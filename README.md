# <img src="frontend/public/radar.svg" alt="RRadar Icon" width="36" style="vertical-align: middle; margin-right: 8px;"> RRadar

RRadar es una aplicación interactiva cliente-servidor diseñada para explorar de manera eficiente Puntos de Interés (POIs) geoespaciales en la ciudad de Lima. 

El núcleo del proyecto es la comparación de rendimiento en las búsquedas espaciales utilizando una **Búsqueda Lineal** frente a la estructura de datos **R-Tree** optimizada para el espacio. 

Este proyecto sirve como aplicación práctica y final para el curso de Algoritmos y Estructuras de Datos.

## 🚀 Características Principales

*   **Implementación de R-Tree Personalizado:** Un backend robusto construido en C++ que implementa desde cero un árbol R-Tree para indexar coordenadas de manera eficiente (R-Tree soporta *Range queries* y consultas *K-Nearest Neighbors - KNN*).
*   **Benchmarking en Tiempo Real:** Realiza comparativas precisas de rendimiento devolviendo tiempos de ejecución de las métricas del R-Tree vs una iteración de búsqueda lineal tradicional.
*   **Visualización en Mapa Web:** Un frontend reactivo en Vue.js y Leaflet que consume la API en C++, permitiendo dibujar y navegar por los *Minimum Bounding Rectangles (MBRs)* y buscar elementos cercanos o dentro de regiones.
*   **Dataset Real:** Inicializado con un conjunto de datos `lima_pois.csv` con locaciones reales en Lima, Perú.

## 🏗 Arquitectura del Proyecto

El proyecto está compuesto por dos partes principales: el `backend` (C++) y el `frontend` (Vue).

```
Proyecto DSA 2/
├── backend/                  # Servidor C++ y motor R-Tree
│   ├── data/                 # Contiene el dataset CSV con los POIs de Lima
│   ├── include/              # Librerías header-only (httplib.h, json.hpp)
│   └── src/                  # Código fuente (RTree.h, main.cpp)
├── frontend/                 # Aplicación Web (Vue + Vite + Leaflet)
│   ├── public/               # Íconos y SVGs
│   └── src/                  # Componentes Vue (MapDashboard.vue, App.vue)
├── ProyectoFinal-AED.pdf     # Documentación / Memoria del Proyecto
└── README.md                 # Este archivo
```

## ⚙️ Tecnologías Utilizadas

### Backend (C++)
*   **C++17** (o superior)
*   **cpp-httplib**: Para levantar el servidor web HTTP de forma minimalista.
*   **nlohmann/json**: Para serializar y deserializar JSON de manera fluida.

### Frontend (JavaScript / Vue)
*   **Vue.js 3**: Framework reactivo de la interfaz.
*   **Vite**: Herramienta de compilación ultrarrápida.
*   **Leaflet & vue-leaflet**: Herramientas principales para la renderización del mapa y marcadores.
*   **Lucide-vue-next**: Para la iconografía de la UI.

---

## 🛠 Instalación y Ejecución

Sigue estos pasos para arrancar el entorno local de desarrollo.

### 1. Iniciar el Servidor Backend (C++)

Asegúrate de tener un compilador de C++ (ej. `g++` o `clang++`) disponible en tu entorno.

```bash
cd backend
# Compilar el código fuente (Ejemplo en Mac/Linux)
g++ src/main.cpp -o server -std=c++17 -O3

# Dar permisos de ejecución e iniciar
chmod +x server
./server
```
El servidor comenzará a ejecutarse en `http://localhost:8080` y precargará los datos desde `data/lima_pois.csv`.

#### Endpoints de la API:
*   `GET /api/ping`: Comprueba el estado del servidor y la cantidad de POIs cargados.
*   `GET /api/mbrs`: Retorna todas las cajas delimitadoras mínimas (MBR) que conforman la estructura actual del árbol.
*   `POST /api/range`: Busca puntos dentro de una zona (body: `{ "minLat", "minLon", "maxLat", "maxLon" }`).
*   `POST /api/knn`: Busca los K puntos más cercanos a una coordenada dada (body: `{ "lat", "lon", "k" }`).

### 2. Iniciar la Interfaz Web (Frontend)

En una nueva ventana de la terminal, asegúrate de tener **Node.js** (v18 o superior) instalado.

```bash
cd frontend
# Instalar dependencias 
npm install

# Iniciar servidor de desarrollo
npm run dev
```

Se te indicará un puerto local (usualmente `http://localhost:5173/`). Abre esta dirección en el navegador para comenzar a interactuar con el mapa.

---

## 👨‍💻 Acerca del Desarrollo

Este proyecto fue desarrollado en respuesta a la necesidad de evaluar el impacto real que tienen las estructuras de datos espaciales (R-Trees) frente al almacenamiento lineal. Al explorar el mapa de Lima desde la app web web, se puede notar visualmente cómo el árbol R-Tree agrupa localizaciones (cajas dibujadas en el mapa) y visita solo un subconjunto de los nodos del árbol frente a la iteración global obligatoria de la búsqueda lineal.
