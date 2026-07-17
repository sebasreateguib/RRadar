#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <vector>

struct SpatialObject {
  int id;
  std::string name;
  double x, y; // x = latitud, y = longitud
  std::string category;
};

// Distancia euclidiana entre dos puntos
inline double pointDistance(double x1, double y1, double x2, double y2) {
  return std::sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

// Rectángulo Delimitador Mínimo (Minimum Bounding Rectangle)
struct MBR {
  double minX, minY, maxX, maxY;

  MBR()
      : minX(std::numeric_limits<double>::max()),
        minY(std::numeric_limits<double>::max()),
        maxX(std::numeric_limits<double>::lowest()),
        maxY(std::numeric_limits<double>::lowest()) {}

  MBR(double mx, double my, double M_x, double M_y)
      : minX(mx), minY(my), maxX(M_x), maxY(M_y) {}

  double area() const {
    if (minX > maxX || minY > maxY)
      return 0;
    return (maxX - minX) * (maxY - minY);
  }

  void expand(const MBR &other) {
    minX = std::min(minX, other.minX);
    minY = std::min(minY, other.minY);
    maxX = std::max(maxX, other.maxX);
    maxY = std::max(maxY, other.maxY);
  }

  void expand(double x, double y) {
    minX = std::min(minX, x);
    minY = std::min(minY, y);
    maxX = std::max(maxX, x);
    maxY = std::max(maxY, y);
  }

  bool contains(double x, double y) const {
    return x >= minX && x <= maxX && y >= minY && y <= maxY;
  }

  bool intersects(const MBR &other) const {
    return !(minX > other.maxX || maxX < other.minX || minY > other.maxY ||
             maxY < other.minY);
  }

  double distanceTo(double px, double py) const {
    double dx = std::max({0.0, minX - px, px - maxX});
    double dy = std::max({0.0, minY - py, py - maxY});
    return std::sqrt(dx * dx + dy * dy);
  }

  static MBR combine(const MBR &a, const MBR &b) {
    MBR res = a;
    res.expand(b);
    return res;
  }
};

class RTreeNode {
public:
  bool isLeaf;
  MBR mbr;
  std::vector<SpatialObject> data;
  std::vector<RTreeNode *> children;
  RTreeNode *parent;

  RTreeNode(bool leaf, RTreeNode *p = nullptr) : isLeaf(leaf), parent(p) {}

  ~RTreeNode() {
    for (auto *child : children) {
      delete child;
    }
  }

  void updateMBR() {
    mbr = MBR(); // Reset
    if (isLeaf) {
      for (const auto &obj : data) {
        mbr.expand(obj.x, obj.y);
      }
    } else {
      for (const auto *child : children) {
        mbr.expand(child->mbr);
      }
    }
  }
};

class RTree {
private:
  RTreeNode *root;
  int MAX_ENTRIES;
  int MIN_ENTRIES;

  RTreeNode *chooseLeaf(RTreeNode *node, const SpatialObject &obj) {
    if (node->isLeaf)
      return node;

    double minEnlargement = std::numeric_limits<double>::max();
    double minArea = std::numeric_limits<double>::max();
    RTreeNode *bestChild = nullptr;

    for (auto *child : node->children) {
      MBR combined = MBR::combine(child->mbr, MBR(obj.x, obj.y, obj.x, obj.y));
      double enlargement = combined.area() - child->mbr.area();
      if (enlargement < minEnlargement ||
          (enlargement == minEnlargement && child->mbr.area() < minArea)) {
        minEnlargement = enlargement;
        minArea = child->mbr.area();
        bestChild = child;
      }
    }
    return chooseLeaf(bestChild, obj);
  }

  // Split Cuadrático Obligatorio (PDF Pag 7) para Hojas
  void quadraticSplitLeaf(RTreeNode *node, RTreeNode *&newNode) {
    newNode = new RTreeNode(true, node->parent);

    // 1. Elegir semillas (PickSeeds)
    int seed1 = 0, seed2 = 1;
    double maxWaste = -1.0;
    for (size_t i = 0; i < node->data.size(); ++i) {
      for (size_t j = i + 1; j < node->data.size(); ++j) {
        MBR m1(node->data[i].x, node->data[i].y, node->data[i].x,
               node->data[i].y);
        MBR m2(node->data[j].x, node->data[j].y, node->data[j].x,
               node->data[j].y);
        MBR combined = MBR::combine(m1, m2);
        double waste = combined.area() - m1.area() - m2.area();
        if (waste > maxWaste) {
          maxWaste = waste;
          seed1 = i;
          seed2 = j;
        }
      }
    }

    std::vector<SpatialObject> allData = node->data;
    node->data.clear();
    node->mbr = MBR();

    // 2. Crear grupos iniciales
    node->data.push_back(allData[seed1]);
    node->updateMBR();
    newNode->data.push_back(allData[seed2]);
    newNode->updateMBR();

    allData.erase(allData.begin() + std::max(seed1, seed2));
    allData.erase(allData.begin() + std::min(seed1, seed2));

    // 3. Distribuir restantes (PickNext)
    while (!allData.empty()) {
      if (node->data.size() + allData.size() == (size_t)MIN_ENTRIES) {
        for (const auto &d : allData)
          node->data.push_back(d);
        break;
      }
      if (newNode->data.size() + allData.size() == (size_t)MIN_ENTRIES) {
        for (const auto &d : allData)
          newNode->data.push_back(d);
        break;
      }

      int bestIdx = -1;
      double maxDiff = -1.0;
      RTreeNode *targetNode = nullptr;

      for (size_t i = 0; i < allData.size(); ++i) {
        MBR dMbr(allData[i].x, allData[i].y, allData[i].x, allData[i].y);
        MBR c1 = MBR::combine(node->mbr, dMbr);
        MBR c2 = MBR::combine(newNode->mbr, dMbr);
        double e1 = c1.area() - node->mbr.area();
        double e2 = c2.area() - newNode->mbr.area();
        double diff = std::abs(e1 - e2);

        if (diff > maxDiff) {
          maxDiff = diff;
          bestIdx = i;
          targetNode =
              (e1 < e2 || (e1 == e2 && node->mbr.area() < newNode->mbr.area()))
                  ? node
                  : newNode;
        }
      }

      targetNode->data.push_back(allData[bestIdx]);
      targetNode->updateMBR();
      allData.erase(allData.begin() + bestIdx);
    }
    node->updateMBR();
    newNode->updateMBR();
  }

  // Split Cuadrático para Nodos Internos
  void quadraticSplitInternal(RTreeNode *node, RTreeNode *&newNode) {
    newNode = new RTreeNode(false, node->parent);

    int seed1 = 0, seed2 = 1;
    double maxWaste = -1.0;
    for (size_t i = 0; i < node->children.size(); ++i) {
      for (size_t j = i + 1; j < node->children.size(); ++j) {
        MBR combined =
            MBR::combine(node->children[i]->mbr, node->children[j]->mbr);
        double waste = combined.area() - node->children[i]->mbr.area() -
                       node->children[j]->mbr.area();
        if (waste > maxWaste) {
          maxWaste = waste;
          seed1 = i;
          seed2 = j;
        }
      }
    }

    std::vector<RTreeNode *> allChildren = node->children;
    node->children.clear();
    node->mbr = MBR();

    node->children.push_back(allChildren[seed1]);
    allChildren[seed1]->parent = node;
    node->updateMBR();

    newNode->children.push_back(allChildren[seed2]);
    allChildren[seed2]->parent = newNode;
    newNode->updateMBR();

    allChildren.erase(allChildren.begin() + std::max(seed1, seed2));
    allChildren.erase(allChildren.begin() + std::min(seed1, seed2));

    while (!allChildren.empty()) {
      if (node->children.size() + allChildren.size() == (size_t)MIN_ENTRIES) {
        for (auto *c : allChildren) {
          c->parent = node;
          node->children.push_back(c);
        }
        break;
      }
      if (newNode->children.size() + allChildren.size() ==
          (size_t)MIN_ENTRIES) {
        for (auto *c : allChildren) {
          c->parent = newNode;
          newNode->children.push_back(c);
        }
        break;
      }

      int bestIdx = -1;
      double maxDiff = -1.0;
      RTreeNode *targetNode = nullptr;

      for (size_t i = 0; i < allChildren.size(); ++i) {
        MBR c1 = MBR::combine(node->mbr, allChildren[i]->mbr);
        MBR c2 = MBR::combine(newNode->mbr, allChildren[i]->mbr);
        double e1 = c1.area() - node->mbr.area();
        double e2 = c2.area() - newNode->mbr.area();
        double diff = std::abs(e1 - e2);

        if (diff > maxDiff) {
          maxDiff = diff;
          bestIdx = i;
          targetNode =
              (e1 < e2 || (e1 == e2 && node->mbr.area() < newNode->mbr.area()))
                  ? node
                  : newNode;
        }
      }

      allChildren[bestIdx]->parent = targetNode;
      targetNode->children.push_back(allChildren[bestIdx]);
      targetNode->updateMBR();
      allChildren.erase(allChildren.begin() + bestIdx);
    }
    node->updateMBR();
    newNode->updateMBR();
  }

  void adjustTree(RTreeNode *node, RTreeNode *splitNode) {
    if (node == root) {
      if (splitNode) {
        RTreeNode *newRoot = new RTreeNode(false);
        newRoot->children.push_back(root);
        newRoot->children.push_back(splitNode);
        root->parent = newRoot;
        splitNode->parent = newRoot;
        newRoot->updateMBR();
        root = newRoot;
      }
      return;
    }

    RTreeNode *parent = node->parent;
    parent->updateMBR();

    RTreeNode *newSplitNode = nullptr;
    if (splitNode) {
      parent->children.push_back(splitNode);
      splitNode->parent = parent;
      if (parent->children.size() > (size_t)MAX_ENTRIES) {
        quadraticSplitInternal(parent, newSplitNode);
      }
    }

    adjustTree(parent, newSplitNode);
  }

  void rangeQueryRecursive(RTreeNode *node, const MBR &range,
                           std::vector<SpatialObject> &results,
                           int &nodesVisited) const {
    nodesVisited++;
    if (!node->mbr.intersects(range))
      return;

    if (node->isLeaf) {
      for (const auto &obj : node->data) {
        if (range.contains(obj.x, obj.y)) {
          results.push_back(obj);
        }
      }
    } else {
      for (auto *child : node->children) {
        rangeQueryRecursive(child, range, results, nodesVisited);
      }
    }
  }

  bool removeRecursive(RTreeNode *node, int id, const MBR &objMBR) {
    if (!node->mbr.intersects(objMBR))
      return false;

    if (node->isLeaf) {
      for (auto it = node->data.begin(); it != node->data.end(); ++it) {
        if (it->id == id) {
          node->data.erase(it);
          node->updateMBR();
          return true;
        }
      }
    } else {
      for (auto *child : node->children) {
        if (removeRecursive(child, id, objMBR)) {
          node->updateMBR();
          return true;
        }
      }
    }
    return false;
  }

  void getAllMBRsRecursive(RTreeNode *node, std::vector<MBR> &rects) const {
    rects.push_back(node->mbr);
    if (!node->isLeaf) {
      for (auto *child : node->children) {
        getAllMBRsRecursive(child, rects);
      }
    }
  }

public:
  RTree(int max_entries = 4)
      : MAX_ENTRIES(max_entries), MIN_ENTRIES(max_entries / 2) {
    root = new RTreeNode(true);
  }

  ~RTree() { delete root; }

  // Insertar un nuevo elemento (Sección 7 del PDF)
  void insert(const SpatialObject &obj) {
    RTreeNode *leaf = chooseLeaf(root, obj);
    leaf->data.push_back(obj);
    leaf->updateMBR();

    RTreeNode *splitNode = nullptr;
    if (leaf->data.size() > (size_t)MAX_ENTRIES) {
      quadraticSplitLeaf(leaf, splitNode);
    }

    adjustTree(leaf, splitNode);
  }

  // Búsqueda por Rango (Sección 7 del PDF)
  std::pair<std::vector<SpatialObject>, int>
  rangeQuery(const MBR &range) const {
    std::vector<SpatialObject> results;
    int nodesVisited = 0;
    rangeQueryRecursive(root, range, results, nodesVisited);
    return {results, nodesVisited};
  }

  // Búsqueda de K Vecinos Más Cercanos (KNN - Sección 7 del PDF)
  std::pair<std::vector<SpatialObject>, int> knnQuery(double x, double y,
                                                      int k) const {
    std::vector<SpatialObject> results;
    int nodesVisited = 0;

    // Priority queue for KNN: min-heap based on distance
    struct QueueElement {
      double dist;
      bool isData;
      RTreeNode *node;
      SpatialObject obj;
      bool operator>(const QueueElement &other) const {
        return dist > other.dist;
      }
    };

    std::priority_queue<QueueElement, std::vector<QueueElement>,
                        std::greater<QueueElement>>
        pq;
    pq.push({root->mbr.distanceTo(x, y), false, root, {}});

    while (!pq.empty() && results.size() < (size_t)k) {
      auto current = pq.top();
      pq.pop();

      if (current.isData) {
        results.push_back(current.obj);
      } else {
        RTreeNode *node = current.node;
        nodesVisited++;
        if (node->isLeaf) {
          for (const auto &obj : node->data) {
            double d = pointDistance(x, y, obj.x, obj.y);
            pq.push({d, true, nullptr, obj});
          }
        } else {
          for (auto *child : node->children) {
            pq.push({child->mbr.distanceTo(x, y), false, child, {}});
          }
        }
      }
    }
    return {results, nodesVisited};
  }

  // Eliminar un objeto (PDF: "eliminar objetos espaciales" y "actualizar
  // correctamente los MBRs")
  bool remove(int id, double x, double y) {
    MBR objMBR(x, y, x, y);
    return removeRecursive(root, id, objMBR);
  }

  // Obtener todos los MBRs para pintarlos en Leaflet
  std::vector<MBR> getAllMBRs() const {
    std::vector<MBR> rects;
    getAllMBRsRecursive(root, rects);
    return rects;
  }
};
