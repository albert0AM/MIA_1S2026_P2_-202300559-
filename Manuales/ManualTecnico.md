# Manual Técnico — MIA Proyecto 2
**Estudiante:** Douglas Alberto Ajú Mucía
**Carnet:** 202300559  
**Curso:** Manejo e Implementación de Archivos

---

## 1. Descripción General

Sistema de simulación de un sistema de archivos EXT2 sobre discos virtuales. El sistema consta de un backend en C++ que procesa comandos, y un frontend en Vue.js que provee la interfaz web.

---

## 2. Arquitectura

```
┌─────────────────────┐        HTTP/REST        ┌──────────────────────┐
│   Frontend (Vue.js) │ ──────────────────────► │  Backend (C++/Crow)  │
│   AWS S3 (estático) │   POST /execute          │  AWS EC2 (Ubuntu)    │
└─────────────────────┘                          └──────────────────────┘
```

- **Frontend:** Vue.js 3, servido como sitio estático en AWS S3.
- **Backend:** C++ con framework Crow, expuesto en el puerto 8080 de una instancia EC2.
- **Comunicación:** El frontend envía los comandos del usuario como texto plano al endpoint `/execute`. El backend los interpreta y retorna la salida como JSON.

---

## 3. Requisitos

### Backend
- g++ con soporte C++17
- Biblioteca Crow (incluida en `vendor/crow/`)
- Pthread
- Sistema operativo Linux (probado en Ubuntu 24.04)

### Frontend
- Node.js >= 18
- npm

---

## 4. Estructura del Proyecto

```
MIA_1S2026_P2_-202300559-/
├── backend/
│   ├── main.cpp               # Punto de entrada, rutas Crow
│   ├── commands/              # Implementación de cada comando
│   │   ├── mkdisk.cpp/h
│   │   ├── fdisk.cpp/h
│   │   ├── mount.cpp/h
│   │   ├── mkfs.cpp/h
│   │   ├── login.cpp/h
│   │   ├── users.cpp/h        # mkgrp, rmgrp, mkusr, rmusr, chgrp
│   │   ├── mkdir.cpp/h
│   │   ├── mkfile.cpp/h
│   │   ├── cat.cpp/h
│   │   ├── rmdisk.cpp/h
│   │   └── reports/           # Generación de reportes Graphviz
│   └── structures/
│       ├── mbr.h              # Estructuras MBR, Partition, EBR
│       └── superblock.h       # Superbloque, inodos, bloques EXT2
└── frontend/
    ├── src/
    │   └── components/
    │       ├── HomeView.vue   # Componente principal
    │       └── config.js      # URL del backend
    └── .env.production        # VITE_API_URL con IP del EC2
```

---

## 5. Estructuras de Datos

### MBR (Master Boot Record)
| Campo | Tipo | Tamaño | Descripción |
|-------|------|--------|-------------|
| mbr_tamano | int32 | 4 B | Tamaño total del disco en bytes |
| mbr_fecha_creacion | char[] | 16 B | Fecha de creación |
| mbr_dsk_signature | int32 | 4 B | Firma aleatoria del disco |
| dsk_fit | char | 1 B | Tipo de ajuste (FF/BF/WF) |
| mbr_partitions | Partition[4] | 140 B | Tabla de particiones |

**Tamaño total MBR: 165 bytes**

### Partition
| Campo | Tipo | Tamaño |
|-------|------|--------|
| part_status | char | 1 B |
| part_type | char | 1 B |
| part_fit | char | 1 B |
| part_start | int32 | 4 B |
| part_s | int32 | 4 B |
| part_name | char[16] | 16 B |
| part_correlative | int32 | 4 B |
| part_id | char[4] | 4 B |

### EXT2
El sistema de archivos implementa las estructuras básicas de EXT2:
- **Superbloque:** Metadatos del sistema de archivos (inodos totales, bloques totales, tamaño de bloque, etc.)
- **Bitmap de inodos y bloques:** Control de espacio libre.
- **Inodos:** Apuntan a bloques de datos y bloques de apuntadores.
- **Bloques de datos:** Almacenan contenido de archivos y directorios.

---

## 6. Compilación y Despliegue

### Backend (EC2)
```bash
# Clonar repositorio
git clone <repo>
cd MIA_1S2026_P2_-202300559-/backend

# Compilar
g++ -std=c++17 -O2 -pthread -I. -Ivendor/crow/include \
    $(find . -name "*.cpp" | grep -v "build/") \
    -o extreamfs

# Ejecutar en background
nohup ./extreamfs > extreamfs.log 2>&1 &
```

### Frontend (S3)
```bash
cd frontend
# Configurar IP del backend
echo "VITE_API_URL=http://<IP_EC2>:8080" > .env.production

npm install
npm run build
# Subir dist/ al bucket S3 manualmente o con AWS CLI:
aws s3 sync dist/ s3://mia-p2-frontend --delete
```

---

## 7. API del Backend

### GET /health
Verifica que el servidor está activo.  
**Respuesta:** `{"status":"ok"}`

### POST /execute
Ejecuta uno o más comandos del sistema de archivos.  
**Body:**
```json
{ "commands": "mkdisk -size=10 -unit=M -path=/tmp/disco.mia" }
```
**Respuesta:**
```json
{ "output": "SUCCESS: disco creado\n  path : /tmp/disco.mia\n..." }
```

---

## 8. Generación de IDs de Partición Montada

Al montar una partición, se asigna un ID automático con el formato:

```
<últimos 2 dígitos del carnet> + <correlativo> + <letra>
```

Ejemplo para carnet 202300559:
- Primera partición del disco 1 → `591A`
- Segunda partición del disco 1 → `592A`
- Primera partición del disco 2 → `591B`

---

## 9. Dependencias Externas

| Dependencia | Uso |
|-------------|-----|
| Crow (C++) | Framework HTTP para el backend |
| Graphviz | Generación de reportes visuales |
| Vue.js 3 | Framework del frontend |
| Axios | Cliente HTTP en el frontend |
| Vite | Bundler del frontend |
