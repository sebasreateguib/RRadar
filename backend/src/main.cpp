#include "../include/httplib.h"
#include "../include/json.hpp"
#include "RTree.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// macOS: obtener directorio del ejecutable (maneja espacios correctamente)
#include <mach-o/dyld.h>
#include <climits>

static std::string execDir() {
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string raw(size, '\0');
  _NSGetExecutablePath(&raw[0], &size);
  // Eliminar el null terminator extra si existe
  while (!raw.empty() && raw.back() == '\0') raw.pop_back();

  // Resolver symlinks con realpath
  char resolved[PATH_MAX];
  if (realpath(raw.c_str(), resolved)) {
    std::string rp(resolved);
    auto pos = rp.rfind('/');
    return (pos != std::string::npos) ? rp.substr(0, pos) : ".";
  }
  // Fallback: dirname manual sobre la cadena raw
  auto pos = raw.rfind('/');
  return (pos != std::string::npos) ? raw.substr(0, pos) : ".";
}


using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────
// Utilidades CSV
// ─────────────────────────────────────────────────────────────────

// Parsea una línea CSV respetando campos entre comillas
static std::vector<std::string> parseCSVLine(const std::string &line) {
  std::vector<std::string> fields;
  std::string field;
  bool inQuotes = false;

  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == '"') {
      if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
        // comilla escapada dentro de campo
        field += '"';
        ++i;
      } else {
        inQuotes = !inQuotes;
      }
    } else if (c == ',' && !inQuotes) {
      fields.push_back(field);
      field.clear();
    } else {
      field += c;
    }
  }
  fields.push_back(field);
  return fields;
}

// Carga el CSV y construye el R-Tree + vector lineal
static int loadData(const std::string &csvPath, RTree &rtree,
                    std::vector<SpatialObject> &allPOIs) {
  std::ifstream file(csvPath);
  if (!file.is_open()) {
    std::cerr << "❌ No se pudo abrir: " << csvPath << std::endl;
    return 0;
  }

  std::string line;
  std::getline(file, line); // saltar cabecera

  int count = 0;
  while (std::getline(file, line)) {
    // Eliminar \r si existe (archivos Windows)
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;

    auto fields = parseCSVLine(line);
    if (fields.size() < 5)
      continue;

    try {
      SpatialObject obj;
      obj.id = std::stoll(fields[0]);
      obj.name = fields[1];
      obj.x = std::stod(fields[2]); // lat
      obj.y = std::stod(fields[3]); // lon
      obj.category = fields[4];

      rtree.insert(obj);
      allPOIs.push_back(obj);
      ++count;
    } catch (...) {
      // línea malformada, ignorar
    }
  }
  return count;
}

// ─────────────────────────────────────────────────────────────────
// Búsqueda lineal de comparación
// ─────────────────────────────────────────────────────────────────

struct LinearRangeResult {
  std::vector<SpatialObject> results;
  double time_ms;
  int elements_checked;
};

static LinearRangeResult linearRange(const std::vector<SpatialObject> &allPOIs,
                                     const MBR &range) {
  auto t0 = std::chrono::high_resolution_clock::now();
  std::vector<SpatialObject> results;
  for (const auto &obj : allPOIs) {
    if (range.contains(obj.x, obj.y))
      results.push_back(obj);
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  double ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
  return {results, ms, (int)allPOIs.size()};
}

struct LinearKNNResult {
  std::vector<std::pair<SpatialObject, double>> results; // {obj, dist}
  double time_ms;
  int elements_checked;
};

static LinearKNNResult linearKNN(const std::vector<SpatialObject> &allPOIs,
                                 double qx, double qy, int k) {
  auto t0 = std::chrono::high_resolution_clock::now();

  std::vector<std::pair<double, size_t>> dists;
  dists.reserve(allPOIs.size());
  for (size_t i = 0; i < allPOIs.size(); ++i) {
    double d = pointDistance(qx, qy, allPOIs[i].x, allPOIs[i].y);
    dists.push_back({d, i});
  }
  std::partial_sort(dists.begin(),
                    dists.begin() + std::min((int)dists.size(), k),
                    dists.end());

  std::vector<std::pair<SpatialObject, double>> results;
  for (int i = 0; i < std::min(k, (int)dists.size()); ++i) {
    results.push_back({allPOIs[dists[i].second], dists[i].first});
  }

  auto t1 = std::chrono::high_resolution_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  return {results, ms, (int)allPOIs.size()};
}

// ─────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────

int main() {
  // Ruta al CSV: data/ está dentro del directorio backend (mismo nivel que el binario)
  const std::string CSV_PATH = execDir() + "/data/lima_pois.csv";

  std::cout << "📂 Cargando POIs desde: " << CSV_PATH << " ..." << std::endl;

  RTree rtree(8); // max 8 entradas por nodo
  std::vector<SpatialObject> allPOIs;

  int loaded = loadData(CSV_PATH, rtree, allPOIs);
  if (loaded == 0) {
    std::cerr << "❌ No se cargaron datos. Verifica la ruta del CSV."
              << std::endl;
    return 1;
  }
  std::cout << "✅ " << loaded << " POIs cargados en el R-Tree." << std::endl;

  // ── Servidor HTTP ──────────────────────────────────────────────
  httplib::Server svr;

  // CORS
  svr.set_post_routing_handler([](const auto &req, auto &res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
  });
  svr.Options(".*", [](const auto &req, auto &res) { res.status = 200; });

  // ── GET /api/ping ──────────────────────────────────────────────
  svr.Get("/api/ping", [&](const httplib::Request &, httplib::Response &res) {
    json r = {{"status", "success"},
              {"message", "Backend C++ activo"},
              {"pois_loaded", loaded}};
    res.set_content(r.dump(), "application/json");
  });

  // ── GET /api/mbrs ──────────────────────────────────────────────
  // Devuelve todos los MBRs del árbol para visualización inicial
  svr.Get("/api/mbrs", [&](const httplib::Request &, httplib::Response &res) {
    auto mbrs = rtree.getAllMBRs();
    json arr = json::array();
    for (const auto &m : mbrs) {
      arr.push_back({{"minLat", m.minX},
                     {"minLon", m.minY},
                     {"maxLat", m.maxX},
                     {"maxLon", m.maxY}});
    }
    json r = {{"mbrs", arr}, {"count", (int)mbrs.size()}};
    res.set_content(r.dump(), "application/json");
  });

  // ── POST /api/range ────────────────────────────────────────────
  // Body: { "minLat": -12.1, "minLon": -77.1, "maxLat": -12.0, "maxLon": -77.0 }
  svr.Post("/api/range",
           [&](const httplib::Request &req, httplib::Response &res) {
             try {
               auto body = json::parse(req.body);
               double minLat = body["minLat"];
               double minLon = body["minLon"];
               double maxLat = body["maxLat"];
               double maxLon = body["maxLon"];

               MBR range(minLat, minLon, maxLat, maxLon);

               // R-Tree query
               auto t0 = std::chrono::high_resolution_clock::now();
               auto [rtreeResults, nodesVisited] = rtree.rangeQuery(range);
               auto t1 = std::chrono::high_resolution_clock::now();
               double rtreeMs =
                   std::chrono::duration<double, std::milli>(t1 - t0).count();

               // Linear query
               auto linResult = linearRange(allPOIs, range);

               // Armar MBRs visitados del R-Tree (los del árbol completo
               // filtrados por intersección con el rango)
               auto allMBRs = rtree.getAllMBRs();
               json mbrArr = json::array();
               for (const auto &m : allMBRs) {
                 if (m.intersects(range)) {
                   mbrArr.push_back({{"minLat", m.minX},
                                     {"minLon", m.minY},
                                     {"maxLat", m.maxX},
                                     {"maxLon", m.maxY}});
                 }
               }

               // Resultados
               json resultsArr = json::array();
               for (const auto &obj : rtreeResults) {
                 resultsArr.push_back({{"id", obj.id},
                                       {"name", obj.name},
                                       {"lat", obj.x},
                                       {"lon", obj.y},
                                       {"category", obj.category}});
               }

               json r = {
                   {"results", resultsArr},
                   {"rtree",
                    {{"time_ms", rtreeMs}, {"nodes_visited", nodesVisited}}},
                   {"linear",
                    {{"time_ms", linResult.time_ms},
                     {"elements_checked", linResult.elements_checked}}},
                   {"mbrs", mbrArr}};

               res.set_content(r.dump(), "application/json");
             } catch (const std::exception &e) {
               json err = {{"error", e.what()}};
               res.status = 400;
               res.set_content(err.dump(), "application/json");
             }
           });

  // ── POST /api/knn ──────────────────────────────────────────────
  // Body: { "lat": -12.046, "lon": -77.042, "k": 10 }
  svr.Post("/api/knn",
           [&](const httplib::Request &req, httplib::Response &res) {
             try {
               auto body = json::parse(req.body);
               double lat = body["lat"];
               double lon = body["lon"];
               int k = body["k"];

               if (k <= 0 || k > 200) k = 10;

               // R-Tree KNN
               auto t0 = std::chrono::high_resolution_clock::now();
               auto [knnResults, nodesVisited] = rtree.knnQuery(lat, lon, k);
               auto t1 = std::chrono::high_resolution_clock::now();
               double rtreeMs =
                   std::chrono::duration<double, std::milli>(t1 - t0).count();

               // Linear KNN
               auto linResult = linearKNN(allPOIs, lat, lon, k);

               // MBRs del árbol cercanos al punto de consulta
               MBR queryRegion(lat - 0.05, lon - 0.05, lat + 0.05, lon + 0.05);
               auto allMBRs = rtree.getAllMBRs();
               json mbrArr = json::array();
               for (const auto &m : allMBRs) {
                 if (m.intersects(queryRegion)) {
                   mbrArr.push_back({{"minLat", m.minX},
                                     {"minLon", m.minY},
                                     {"maxLat", m.maxX},
                                     {"maxLon", m.maxY}});
                 }
               }

               // Distancia real en metros (aprox) usando fórmula simple
               // 1 grado lat ≈ 111,000 m, 1 grado lon ≈ 111,000 * cos(lat) m
               const double LAT_M = 111000.0;
               const double LON_M = 111000.0 * std::cos(lat * M_PI / 180.0);

               json resultsArr = json::array();
               for (const auto &obj : knnResults) {
                 double dlat = (obj.x - lat) * LAT_M;
                 double dlon = (obj.y - lon) * LON_M;
                 double distM = std::sqrt(dlat * dlat + dlon * dlon);
                 resultsArr.push_back({{"id", obj.id},
                                       {"name", obj.name},
                                       {"lat", obj.x},
                                       {"lon", obj.y},
                                       {"category", obj.category},
                                       {"distance", distM}});
               }

               json r = {
                   {"results", resultsArr},
                   {"rtree",
                    {{"time_ms", rtreeMs}, {"nodes_visited", nodesVisited}}},
                   {"linear",
                    {{"time_ms", linResult.time_ms},
                     {"elements_checked", linResult.elements_checked}}},
                   {"mbrs", mbrArr}};

               res.set_content(r.dump(), "application/json");
             } catch (const std::exception &e) {
               json err = {{"error", e.what()}};
               res.status = 400;
               res.set_content(err.dump(), "application/json");
             }
           });

  std::cout << "🚀 Servidor C++ API corriendo en http://localhost:8080"
            << std::endl;
  std::cout << "   Endpoints disponibles:" << std::endl;
  std::cout << "   GET  /api/ping" << std::endl;
  std::cout << "   GET  /api/mbrs" << std::endl;
  std::cout << "   POST /api/range  { minLat, minLon, maxLat, maxLon }"
            << std::endl;
  std::cout << "   POST /api/knn    { lat, lon, k }" << std::endl;

  svr.listen("0.0.0.0", 8080);

  return 0;
}
