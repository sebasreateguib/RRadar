<template>
  <div class="dashboard-container">
    <!-- Loading Overlay -->
    <Transition name="fade">
      <div v-if="isLoading" class="loading-overlay">
        <div class="spinner-ring"></div>
        <span>Consultando R-Tree...</span>
      </div>
    </Transition>

    <!-- Floating Panel -->
    <aside class="floating-panel glass-panel">
      <div class="sidebar-header">
        <div class="logo-container">
          <svg class="radar-logo" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
            <circle cx="50" cy="50" r="40" fill="none" stroke="#0ea5e9" stroke-width="2" stroke-opacity="0.3"/>
            <circle cx="50" cy="50" r="25" fill="none" stroke="#0ea5e9" stroke-width="2" stroke-opacity="0.5"/>
            <circle cx="50" cy="50" r="6" fill="#0ea5e9"/>
            <path class="radar-sweep" d="M50 50 L50 10 A40 40 0 0 1 84.6 30 Z" fill="url(#sweep-grad)" />
            <rect x="15" y="20" width="40" height="35" fill="none" stroke="#38bdf8" stroke-width="2" stroke-dasharray="4" rx="2"/>
            <rect x="45" y="55" width="35" height="25" fill="none" stroke="#22c55e" stroke-width="2" stroke-dasharray="4" rx="2"/>
            <defs>
              <linearGradient id="sweep-grad" x1="0%" y1="0%" x2="100%" y2="100%">
                <stop offset="0%" stop-color="#0ea5e9" stop-opacity="0.4" />
                <stop offset="100%" stop-color="#0ea5e9" stop-opacity="0" />
              </linearGradient>
            </defs>
          </svg>
          <h1 class="title">R-Radar</h1>
        </div>
        <p class="subtitle">Spatial Search Engine • Lima</p>
      </div>

      <div class="controls-section">
        <h3>Modo de Consulta</h3>
        <div class="button-group">
          <button 
            class="btn" 
            :class="{ active: mode === 'nav' }" 
            @click="setMode('nav')"
          >
            <Hand :size="18" />
            Navegar Mapa
          </button>
          <button 
            class="btn" 
            :class="{ active: mode === 'range' }" 
            @click="setMode('range')"
          >
            <SquareDashed :size="18" />
            Rango (Arrastrar)
          </button>
          <button 
            class="btn" 
            :class="{ active: mode === 'knn' }" 
            @click="setMode('knn')"
          >
            <Target :size="18" />
            K-NN (Click)
          </button>
        </div>

        <div v-if="mode === 'knn'" class="slider-container">
          <label>Cantidad de Vecinos (K = {{ kValue }})</label>
          <input type="range" v-model="kValue" min="1" max="50" class="styled-slider" />
        </div>
      </div>

      <div class="metrics-section">
        <h3>Comparación de Rendimiento</h3>
        <div class="metric-card rtree" :class="{ 'highlight': lastQuery !== null }">
          <div class="metric-header">
            <span class="dot blue"></span>
            <span>R-Tree Index</span>
          </div>
          <div class="metric-value">{{ metrics.rtreeTime.toFixed(3) }} ms</div>
          <div class="metric-sub">{{ metrics.rtreeNodes }} nodos visitados</div>
        </div>

        <div class="metric-card brute" :class="{ 'highlight': lastQuery !== null }">
          <div class="metric-header">
            <span class="dot red"></span>
            <span>Búsqueda Lineal</span>
          </div>
          <div class="metric-value">{{ metrics.linearTime.toFixed(3) }} ms</div>
          <div class="metric-sub">{{ metrics.linearNodes.toLocaleString() }} elementos revisados</div>
        </div>

        <div v-if="lastQuery !== null" class="speedup-badge">
          <Zap :size="14" />
          {{ speedup }}x más rápido con R-Tree
        </div>
      </div>
      
      <div class="results-section">
        <h3>Resultados ({{ results.length }})</h3>
        <ul class="results-list">
          <li v-for="res in results" :key="res.id" class="result-item" @click="flyToResult(res)">
            <div class="category-icon" :style="{ background: categoryColor(res.category) }">
              {{ categoryEmoji(res.category) }}
            </div>
            <div class="res-info">
              <strong>{{ res.name }}</strong>
              <span>{{ res.category }}</span>
            </div>
            <div class="res-dist" v-if="res.distance != null">{{ formatDist(res.distance) }}</div>
          </li>
          <li v-if="results.length === 0 && !isLoading" class="empty-state">
            Haz una consulta en el mapa para ver resultados.
          </li>
        </ul>
      </div>
    </aside>

    <!-- Main Map Area -->
    <main class="map-area">
      <div id="map" ref="mapContainer"></div>
      
      <!-- Floating tooltip -->
      <div class="floating-toolbar glass-panel">
        <span class="pulse-dot" :class="{ 'error-dot': apiError }"></span>
        <span v-if="apiError" class="error-text">⚠️ Backend no disponible. Asegúrate de que el servidor C++ está corriendo en :8080</span>
        <span v-else-if="mode === 'nav'">Selecciona un modo de búsqueda en el panel.</span>
        <span v-else-if="mode === 'range'">Dibuja un rectángulo en el mapa para buscar locales.</span>
        <span v-else-if="mode === 'knn'">Haz clic en cualquier punto para encontrar los {{ kValue }} locales más cercanos.</span>
      </div>
    </main>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, shallowRef } from 'vue';
import { SquareDashed, Target, Hand, Zap } from 'lucide-vue-next';
import L from 'leaflet';

const API_BASE = 'http://localhost:8080';

const mapContainer = ref(null);
const map = shallowRef(null);
const mode = ref('nav');
const kValue = ref(5);
const results = ref([]);
const isLoading = ref(false);
const apiError = ref(false);
const lastQuery = ref(null); // 'range' | 'knn'

const metrics = ref({
  rtreeTime: 0.00,
  rtreeNodes: 0,
  linearTime: 0.00,
  linearNodes: 0
});

// Computed: aceleración del R-Tree sobre la búsqueda lineal
const speedup = computed(() => {
  if (metrics.value.rtreeTime <= 0) return '—';
  return (metrics.value.linearTime / metrics.value.rtreeTime).toFixed(1);
});

// Layers
const markersLayer = shallowRef(null);
const mbrLayer = shallowRef(null);
const queryLayer = shallowRef(null);

// Ref de marcadores para poder hacer fly-to
const markerRefs = {};

// Map interactions variables
let isDrawing = false;
let startPoint = null;
let currentRect = null;

// ─────────────────────────────────────────────────────────────
// Categorías
// ─────────────────────────────────────────────────────────────

const CAT_EMOJI = {
  restaurant: '🍽️', fast_food: '🍔', cafe: '☕', bar: '🍺', pub: '🍻',
  bank: '🏦', atm: '💳', pharmacy: '💊', clinic: '🏥', doctors: '👨‍⚕️',
  hospital: '🏨', school: '🏫', college: '🎓', kindergarten: '🧒',
  supermarket: '🛒', mall: '🏬', convenience: '🏪', fuel: '⛽',
  bus_station: '🚌', place_of_worship: '⛪', park: '🌳', cinema: '🎬',
  museum: '🏛️', library: '📚', hotel: '🏨', sports_centre: '⚽',
  theatre: '🎭', nightclub: '🎉', doityourself: '🔨', electronics: '📱',
};

const CAT_COLORS = {
  restaurant: '#f97316', fast_food: '#fb923c', cafe: '#a78bfa',
  bar: '#f59e0b', bank: '#3b82f6', pharmacy: '#10b981',
  clinic: '#06b6d4', doctors: '#0ea5e9', school: '#8b5cf6',
  supermarket: '#22c55e', fuel: '#ef4444', bus_station: '#64748b',
};

function categoryEmoji(cat) {
  return CAT_EMOJI[cat] || '📍';
}

function categoryColor(cat) {
  return CAT_COLORS[cat] || '#6b7280';
}

// ─────────────────────────────────────────────────────────────
// Modos e interacción del mapa
// ─────────────────────────────────────────────────────────────

const setMode = (newMode) => {
  mode.value = newMode;
  clearMap();
  if (map.value) {
    if (newMode === 'range') {
      map.value.dragging.disable();
    } else {
      map.value.dragging.enable();
    }
  }
};

const clearMap = () => {
  markersLayer.value.clearLayers();
  mbrLayer.value.clearLayers();
  queryLayer.value.clearLayers();
  results.value = [];
  lastQuery.value = null;
  metrics.value = { rtreeTime: 0, rtreeNodes: 0, linearTime: 0, linearNodes: 0 };
  if (currentRect) {
    currentRect.remove();
    currentRect = null;
  }
};

// ─────────────────────────────────────────────────────────────
// Llamadas a la API real
// ─────────────────────────────────────────────────────────────

async function searchRange(bounds) {
  isLoading.value = true;
  apiError.value = false;
  try {
    const body = {
      minLat: bounds.getSouth(),
      minLon: bounds.getWest(),
      maxLat: bounds.getNorth(),
      maxLon: bounds.getEast(),
    };

    const resp = await fetch(`${API_BASE}/api/range`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });

    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const data = await resp.json();

    renderResults(data, 'range');
  } catch (err) {
    console.error('Error en /api/range:', err);
    apiError.value = true;
  } finally {
    isLoading.value = false;
  }
}

async function searchKNN(latlng, k) {
  isLoading.value = true;
  apiError.value = false;
  try {
    const body = { lat: latlng.lat, lon: latlng.lng, k };

    const resp = await fetch(`${API_BASE}/api/knn`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });

    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const data = await resp.json();

    renderResults(data, 'knn', latlng);
  } catch (err) {
    console.error('Error en /api/knn:', err);
    apiError.value = true;
  } finally {
    isLoading.value = false;
  }
}

// ─────────────────────────────────────────────────────────────
// Render de resultados en el mapa
// ─────────────────────────────────────────────────────────────

function renderResults(data, queryType, queryPoint = null) {
  markersLayer.value.clearLayers();
  mbrLayer.value.clearLayers();
  results.value = [];

  // MBRs visitados (rectangles del R-Tree)
  if (data.mbrs && data.mbrs.length > 0) {
    data.mbrs.forEach((m, i) => {
      const opacity = Math.max(0.03, 0.15 - i * 0.005);
      L.rectangle(
        [[m.minLat, m.minLon], [m.maxLat, m.maxLon]],
        {
          color: '#0ea5e9',
          weight: 1,
          fillOpacity: opacity,
          dashArray: '4',
          interactive: false,
        }
      ).addTo(mbrLayer.value);
    });
  }

  // Marcadores de POIs encontrados
  data.results.forEach((poi) => {
    const color = categoryColor(poi.category);
    const emoji = categoryEmoji(poi.category);

    // Icono custom con emoji
    const icon = L.divIcon({
      html: `<div class="poi-marker" style="background:${color};">${emoji}</div>`,
      className: '',
      iconSize: [32, 32],
      iconAnchor: [16, 16],
    });

    const marker = L.marker([poi.lat, poi.lon], { icon })
      .addTo(markersLayer.value);

    const distLabel = poi.distance != null
      ? `<br><span style="color:#38bdf8">${formatDist(poi.distance)}</span>`
      : '';

    marker.bindPopup(
      `<b>${poi.name}</b><br><span style="opacity:0.7">${poi.category}</span>${distLabel}`,
      { className: 'dark-popup' }
    );

    results.value.push(poi);
    markerRefs[poi.id] = marker;

    // Líneas hacia el punto de consulta (KNN)
    if (queryType === 'knn' && queryPoint) {
      L.polyline([[queryPoint.lat, queryPoint.lng], [poi.lat, poi.lon]], {
        color: color,
        weight: 1.5,
        opacity: 0.4,
        dashArray: '5, 5',
      }).addTo(queryLayer.value);
    }
  });

  // Métricas
  metrics.value = {
    rtreeTime: data.rtree.time_ms,
    rtreeNodes: data.rtree.nodes_visited,
    linearTime: data.linear.time_ms,
    linearNodes: data.linear.elements_checked,
  };

  lastQuery.value = queryType;
}

// Fly-to en el mapa al hacer click en un resultado
function flyToResult(res) {
  if (map.value) {
    map.value.setView([res.lat, res.lon], 17, { animate: true });
    const marker = markerRefs[res.id];
    if (marker) marker.openPopup();
  }
}

// ─────────────────────────────────────────────────────────────
// Formateo
// ─────────────────────────────────────────────────────────────

function formatDist(meters) {
  if (meters < 1000) return `${Math.round(meters)} m`;
  return `${(meters / 1000).toFixed(2)} km`;
}

// ─────────────────────────────────────────────────────────────
// Eventos del mapa
// ─────────────────────────────────────────────────────────────

const onMapMouseDown = (e) => {
  if (mode.value === 'range') {
    isDrawing = true;
    startPoint = e.latlng;
    if (currentRect) map.value.removeLayer(currentRect);
    currentRect = L.rectangle([startPoint, startPoint], {
      color: '#22c55e', weight: 2, fillOpacity: 0.1
    }).addTo(map.value);
  } else if (mode.value === 'knn') {
    clearMap();
    queryLayer.value.clearLayers();
    // Punto de consulta
    L.circleMarker(e.latlng, {
      radius: 10,
      fillColor: '#ef4444',
      color: '#fff',
      weight: 2,
      fillOpacity: 1
    }).addTo(queryLayer.value);

    searchKNN(e.latlng, Number(kValue.value));
  }
};

const onMapMouseMove = (e) => {
  if (mode.value === 'range' && isDrawing && currentRect) {
    currentRect.setBounds([startPoint, e.latlng]);
  }
};

const onMapMouseUp = (e) => {
  if (mode.value === 'range' && isDrawing) {
    isDrawing = false;
    if (startPoint.distanceTo(e.latlng) > 50) {
      queryLayer.value.clearLayers();
      // Dibujar el rectángulo de consulta permanente
      L.rectangle(currentRect.getBounds(), {
        color: '#22c55e',
        weight: 2,
        fillOpacity: 0.05,
        dashArray: '6',
      }).addTo(queryLayer.value);
      searchRange(currentRect.getBounds());
    } else {
      if (currentRect) map.value.removeLayer(currentRect);
      currentRect = null;
    }
  }
};

// ─────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────

onMounted(() => {
  // Inicializar mapa Leaflet centrado en Lima
  map.value = L.map(mapContainer.value, {
    center: [-12.046374, -77.042793],
    zoom: 14,
    zoomControl: false
  });

  // Tiles oscuros CartoDB
  L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
    attribution: '&copy; OpenStreetMap contributors &copy; CARTO',
    subdomains: 'abcd',
    maxZoom: 19
  }).addTo(map.value);

  L.control.zoom({ position: 'bottomright' }).addTo(map.value);

  // Grupos de capas
  markersLayer.value = L.layerGroup().addTo(map.value);
  mbrLayer.value = L.layerGroup().addTo(map.value);
  queryLayer.value = L.layerGroup().addTo(map.value);

  // Eventos
  map.value.on('mousedown', onMapMouseDown);
  map.value.on('mousemove', onMapMouseMove);
  map.value.on('mouseup', onMapMouseUp);

  // Estilos de popups en el mapa
  injectPopupStyles();
});

function injectPopupStyles() {
  const style = document.createElement('style');
  style.textContent = `
    .poi-marker {
      width: 32px; height: 32px;
      border-radius: 50%;
      display: flex; align-items: center; justify-content: center;
      font-size: 15px;
      border: 2px solid rgba(255,255,255,0.4);
      box-shadow: 0 2px 8px rgba(0,0,0,0.5);
      cursor: pointer;
      transition: transform 0.15s;
    }
    .poi-marker:hover { transform: scale(1.2); }
    .dark-popup .leaflet-popup-content-wrapper {
      background: #1e293b;
      color: #e2e8f0;
      border: 1px solid rgba(255,255,255,0.1);
      border-radius: 10px;
      box-shadow: 0 4px 20px rgba(0,0,0,0.5);
    }
    .dark-popup .leaflet-popup-tip { background: #1e293b; }
    .dark-popup .leaflet-popup-content { margin: 12px 16px; font-size: 13px; }
  `;
  document.head.appendChild(style);
}
</script>

<style scoped>
.dashboard-container {
  position: relative;
  width: 100vw;
  height: 100vh;
}

/* ── Loading Overlay ── */
.loading-overlay {
  position: fixed;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  z-index: 2000;
  background: rgba(15, 23, 42, 0.85);
  backdrop-filter: blur(12px);
  border: 1px solid rgba(14, 165, 233, 0.3);
  border-radius: 16px;
  padding: 24px 40px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
  color: #e2e8f0;
  font-size: 14px;
  font-weight: 500;
  box-shadow: 0 8px 40px rgba(0,0,0,0.6);
}

.spinner-ring {
  width: 36px;
  height: 36px;
  border: 3px solid rgba(14, 165, 233, 0.2);
  border-top-color: #0ea5e9;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin { to { transform: rotate(360deg); } }

.fade-enter-active, .fade-leave-active { transition: opacity 0.2s; }
.fade-enter-from, .fade-leave-to { opacity: 0; }

/* ── Panel Flotante ── */
.floating-panel {
  position: absolute;
  top: 24px;
  left: 24px;
  width: 320px;
  max-height: calc(100vh - 48px);
  z-index: 1000;
  display: flex;
  flex-direction: column;
  padding: 20px;
  gap: 20px;
  border-radius: 16px;
  border: 1px solid var(--panel-border);
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
  overflow: hidden;
}

.sidebar-header .title {
  font-size: 22px;
  font-weight: 700;
  background: linear-gradient(90deg, #0ea5e9, #38bdf8);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  margin: 0;
}

.logo-container {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 6px;
}

.radar-logo {
  width: 36px;
  height: 36px;
}

.radar-sweep {
  transform-origin: 50px 50px;
  animation: sweep 4s linear infinite;
}

@keyframes sweep {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.sidebar-header .subtitle {
  font-size: 12px;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 1px;
}

h3 {
  font-size: 13px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-muted);
  margin-bottom: 12px;
}

.button-group {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.slider-container {
  margin-top: 16px;
}
.slider-container label {
  display: block;
  font-size: 13px;
  margin-bottom: 8px;
  color: var(--text-main);
}
.styled-slider {
  width: 100%;
  accent-color: var(--accent-color);
}

/* ── Métricas ── */
.metrics-section {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.metric-card {
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  padding: 16px;
  border: 1px solid var(--panel-border);
  transition: border-color 0.3s;
}

.metric-card.highlight.rtree {
  border-color: rgba(14, 165, 233, 0.4);
  box-shadow: 0 0 12px rgba(14, 165, 233, 0.1);
}

.metric-card.highlight.brute {
  border-color: rgba(239, 68, 68, 0.3);
}

.metric-header {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  font-weight: 500;
  margin-bottom: 8px;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}
.dot.blue { background: var(--accent-color); box-shadow: 0 0 8px var(--accent-color); }
.dot.red { background: var(--danger); }

.metric-value {
  font-size: 22px;
  font-weight: 700;
  font-family: monospace;
}

.metric-sub {
  font-size: 12px;
  color: var(--text-muted);
  margin-top: 4px;
}

.speedup-badge {
  display: flex;
  align-items: center;
  gap: 6px;
  background: linear-gradient(135deg, rgba(14, 165, 233, 0.15), rgba(34, 197, 94, 0.15));
  border: 1px solid rgba(14, 165, 233, 0.3);
  border-radius: 8px;
  padding: 8px 12px;
  font-size: 13px;
  font-weight: 600;
  color: #38bdf8;
}

/* ── Resultados ── */
.results-section {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  min-height: 0;
}

.results-list {
  list-style: none;
  overflow-y: auto;
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding-right: 4px;
  padding-bottom: 8px;
}

.results-list::-webkit-scrollbar { width: 4px; }
.results-list::-webkit-scrollbar-track { background: transparent; }
.results-list::-webkit-scrollbar-thumb { background: rgba(255,255,255,0.1); border-radius: 2px; }

.result-item {
  display: flex;
  align-items: center;
  gap: 10px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(255, 255, 255, 0.05);
  padding: 10px 12px;
  border-radius: 8px;
  cursor: pointer;
  transition: background 0.2s, transform 0.15s;
}

.result-item:hover {
  background: rgba(255, 255, 255, 0.08);
  transform: translateX(2px);
}

.category-icon {
  width: 30px;
  height: 30px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  flex-shrink: 0;
  opacity: 0.85;
}

.res-info {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}
.res-info strong {
  font-size: 13px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.res-info span {
  font-size: 11px;
  color: var(--text-muted);
  text-transform: capitalize;
}

.res-dist {
  font-size: 11px;
  font-family: monospace;
  color: var(--accent-hover);
  white-space: nowrap;
  flex-shrink: 0;
}

.empty-state {
  text-align: center;
  padding: 24px;
  font-size: 13px;
  color: var(--text-muted);
  border: 1px dashed var(--panel-border);
  border-radius: 8px;
}

/* ── Área del mapa ── */
.map-area {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  z-index: 1;
}

#map {
  width: 100%;
  height: 100%;
}

/* ── Toolbar flotante inferior ── */
.floating-toolbar {
  position: absolute;
  bottom: 32px;
  left: 50%;
  transform: translateX(-50%);
  z-index: 1000;
  padding: 12px 24px;
  border-radius: 30px;
  display: flex;
  align-items: center;
  gap: 12px;
  font-size: 14px;
  font-weight: 500;
  max-width: 520px;
  text-align: center;
}

.error-text {
  color: #fca5a5;
  font-size: 13px;
}

.pulse-dot {
  width: 10px;
  height: 10px;
  background: var(--success);
  border-radius: 50%;
  flex-shrink: 0;
  animation: pulse 2s infinite;
}

.pulse-dot.error-dot {
  background: var(--danger);
  animation: none;
}

@keyframes pulse {
  0% { box-shadow: 0 0 0 0 rgba(34, 197, 94, 0.4); }
  70% { box-shadow: 0 0 0 10px rgba(34, 197, 94, 0); }
  100% { box-shadow: 0 0 0 0 rgba(34, 197, 94, 0); }
}
</style>
