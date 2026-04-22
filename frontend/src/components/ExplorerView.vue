<template>
  <div class="explorer-page">

    <!-- ── Navbar ── -->
    <nav class="navbar">
      <div class="nav-left">
        <span class="nav-logo">
          <span class="bracket">[</span>ExtreamFS<span class="bracket">]</span>
        </span>
        <span class="nav-sep">/</span>
        <span class="nav-breadcrumb">{{ breadcrumb }}</span>
      </div>
      <div class="nav-right">
        <span v-if="session.active" class="session-badge">
          ● {{ session.username }} — {{ session.partId }}
        </span>
        <button class="btn-nav" @click="$router.push('/')">Terminal</button>
        <button class="btn-nav btn-journal" @click="$router.push('/journaling')">Journal</button>
        <button class="btn-nav btn-logout" @click="logout" v-if="session.active">
          Cerrar Sesión
        </button>
      </div>
    </nav>

    <!-- ── Paso 1: Selección de disco ── -->
    <div v-if="step === 'disks'" class="container">
      <h2 class="section-title">Selecciona un disco</h2>
      <div v-if="loadingDisks" class="loading">Cargando discos...</div>
      <div v-else-if="disks.length === 0" class="empty">No hay discos disponibles. Crea uno desde la terminal.</div>
      <div v-else class="grid">
        <div
          v-for="disk in disks"
          :key="disk.path"
          class="card disk-card"
          @click="selectDisk(disk)"
        >
          <div class="card-icon">💾</div>
          <div class="card-name">{{ diskName(disk.path) }}</div>
          <div class="card-meta">
            <span>{{ formatSize(disk.size) }}</span>
            <span class="badge">Fit: {{ disk.fit }}</span>
          </div>
          <div class="card-parts" v-if="disk.partitions.length > 0">
            <span v-for="p in disk.partitions" :key="p" class="part-tag">{{ p }}</span>
          </div>
          <div class="card-parts empty-parts" v-else>Sin particiones montadas</div>
        </div>
      </div>
    </div>

    <!-- ── Paso 2: Selección de partición ── -->
    <div v-else-if="step === 'partitions'" class="container">
      <div class="back-bar">
        <button class="btn-back" @click="step = 'disks'">← Discos</button>
        <span class="back-title">{{ diskName(selectedDisk.path) }}</span>
      </div>
      <h2 class="section-title">Selecciona una partición</h2>
      <div v-if="loadingParts" class="loading">Cargando particiones...</div>
      <div v-else-if="partitions.length === 0" class="empty">No hay particiones en este disco.</div>
      <div v-else class="grid">
        <div
          v-for="part in partitions"
          :key="part.id"
          class="card part-card"
          :class="{ 'unmounted': part.status !== '1' }"
          @click="selectPartition(part)"
        >
          <div class="card-icon">🗂️</div>
          <div class="card-name">{{ part.name }}</div>
          <div class="card-meta">
            <span>{{ formatSize(part.size) }}</span>
            <span class="badge" :class="part.status === '1' ? 'badge-ok' : 'badge-off'">
              {{ part.status === '1' ? 'Montada' : 'Desmontada' }}
            </span>
          </div>
          <div class="card-meta">
            <span class="badge">Tipo: {{ part.type }}</span>
            <span class="badge">Fit: {{ part.fit }}</span>
            <span class="badge badge-id" v-if="part.id">ID: {{ part.id }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- ── Paso 3: Navegación del sistema de archivos ── -->
    <div v-else-if="step === 'files'" class="container">
      <div class="back-bar">
        <button class="btn-back" @click="goBackPartitions">← Particiones</button>
        <span class="back-title">{{ selectedPartition.id }}</span>
      </div>

      <!-- Barra de path -->
      <div class="path-bar">
        <span class="path-icon">📁</span>
        <span
          v-for="(seg, i) in pathSegments"
          :key="i"
          class="path-seg"
          @click="navigateTo(pathUpTo(i))"
        >
          <span class="path-sep" v-if="i > 0"> / </span>
          {{ seg }}
        </span>
      </div>

      <div v-if="loadingFiles" class="loading">Cargando...</div>
      <div v-else-if="fileError" class="empty error-msg">{{ fileError }}</div>
      <div v-else-if="entries.length === 0" class="empty">Carpeta vacía</div>
      <div v-else class="file-grid">
        <div
          v-for="entry in entries"
          :key="entry.name"
          class="file-card"
          :class="entry.type === '0' ? 'is-folder' : 'is-file'"
          @click="handleEntryClick(entry)"
        >
          <div class="file-icon">{{ entry.type === '0' ? '📁' : '📄' }}</div>
          <div class="file-name">{{ entry.name }}</div>
          <div class="file-meta">
            <span class="perm">{{ entry.perm }}</span>
            <span class="size" v-if="entry.type === '1'">{{ formatSize(entry.size) }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- ── Paso 4: Vista de archivo ── -->
    <div v-else-if="step === 'fileview'" class="container">
      <div class="back-bar">
        <button class="btn-back" @click="step = 'files'">← {{ currentPath }}</button>
        <span class="back-title">{{ viewingFile }}</span>
      </div>
      <div class="file-viewer">
        <div class="viewer-header">
          <span class="viewer-filename">📄 {{ viewingFile }}</span>
        </div>
        <div v-if="loadingContent" class="loading">Cargando archivo...</div>
        <pre v-else class="viewer-content">{{ fileContent }}</pre>
      </div>
    </div>

  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import axios from 'axios'
import BASE_URL from './config.js'

const router = useRouter()

// ── Estado de navegación ──────────────────────────────────────
const step              = ref('disks')
const disks             = ref([])
const partitions        = ref([])
const entries           = ref([])
const selectedDisk      = ref(null)
const selectedPartition = ref(null)
const currentPath       = ref('/')
const fileContent       = ref('')
const viewingFile       = ref('')
const fileError         = ref('')

// ── Loading flags ─────────────────────────────────────────────
const loadingDisks   = ref(false)
const loadingParts   = ref(false)
const loadingFiles   = ref(false)
const loadingContent = ref(false)

// ── Sesión ────────────────────────────────────────────────────
const session = ref({ active: false, username: '', partId: '' })

onMounted(() => {
  const saved = localStorage.getItem('session')
  if (saved) session.value = JSON.parse(saved)
  loadDisks()
})

// ── Breadcrumb ────────────────────────────────────────────────
const breadcrumb = computed(() => {
  if (step.value === 'disks')      return 'Discos'
  if (step.value === 'partitions') return diskName(selectedDisk.value?.path)
  if (step.value === 'files')      return `${selectedPartition.value?.id}${currentPath.value}`
  if (step.value === 'fileview')   return viewingFile.value
  return ''
})

// ── Segmentos del path actual ─────────────────────────────────
const pathSegments = computed(() => {
  if (currentPath.value === '/') return ['/']
  return ['/', ...currentPath.value.split('/').filter(Boolean)]
})

function pathUpTo(idx) {
  if (idx === 0) return '/'
  const segs = currentPath.value.split('/').filter(Boolean)
  return '/' + segs.slice(0, idx).join('/')
}

// ── Helpers ───────────────────────────────────────────────────
function diskName(path) {
  if (!path) return ''
  return path.split('/').pop()
}

function formatSize(bytes) {
  if (!bytes) return '0 B'
  if (bytes < 1024)       return bytes + ' B'
  if (bytes < 1024*1024)  return (bytes/1024).toFixed(1) + ' KB'
  return (bytes/(1024*1024)).toFixed(1) + ' MB'
}

// ── Cargar discos ─────────────────────────────────────────────
async function loadDisks() {
  loadingDisks.value = true
  try {
    const res = await axios.get(`${BASE_URL}/disks`)
    disks.value = res.data.disks || []
  } catch {
    disks.value = []
  } finally {
    loadingDisks.value = false
  }
}

// ── Seleccionar disco ─────────────────────────────────────────
async function selectDisk(disk) {
  selectedDisk.value = disk
  step.value = 'partitions'
  loadingParts.value = true
  try {
    const res = await axios.get(`${BASE_URL}/partitions`, {
      params: { disk: disk.path }
    })
    partitions.value = res.data.partitions || []
  } catch {
    partitions.value = []
  } finally {
    loadingParts.value = false
  }
}

// ── Seleccionar partición ─────────────────────────────────────
async function selectPartition(part) {
  if (part.status !== '1') {
    alert(`La partición "${part.name}" no está montada. Monta primero con el comando mount.`)
    return
  }
  selectedPartition.value = part
  currentPath.value = '/'
  step.value = 'files'
  await loadFolder('/')
}

function goBackPartitions() {
  step.value = 'partitions'
  currentPath.value = '/'
  entries.value = []
}

// ── Cargar contenido de carpeta ───────────────────────────────
async function loadFolder(path) {
  loadingFiles.value = true
  fileError.value = ''
  try {
    const res = await axios.get(`${BASE_URL}/ls`, {
      params: { id: selectedPartition.value.id, path }
    })
    entries.value = res.data.entries || []
  } catch (e) {
    fileError.value = e.response?.data?.error || 'Error al cargar la carpeta'
    entries.value = []
  } finally {
    loadingFiles.value = false
  }
}

// ── Navegar a path ────────────────────────────────────────────
async function navigateTo(path) {
  currentPath.value = path
  await loadFolder(path)
}

// ── Click en entrada ──────────────────────────────────────────
async function handleEntryClick(entry) {
  if (entry.type === '0') {
    // Es carpeta — navegar dentro
    const newPath = currentPath.value === '/'
      ? '/' + entry.name
      : currentPath.value + '/' + entry.name
    currentPath.value = newPath
    await loadFolder(newPath)
  } else {
    // Es archivo — ver contenido
    await viewFile(entry.name)
  }
}

// ── Ver contenido de archivo ──────────────────────────────────
async function viewFile(name) {
  const filePath = currentPath.value === '/'
    ? '/' + name
    : currentPath.value + '/' + name

  viewingFile.value = name
  step.value = 'fileview'
  loadingContent.value = true
  fileContent.value = ''

  try {
    const res = await axios.get(`${BASE_URL}/cat`, {
      params: { id: selectedPartition.value.id, path: filePath }
    })
    fileContent.value = res.data.content || '(archivo vacío)'
  } catch (e) {
    fileContent.value = 'Error al leer el archivo: ' + (e.response?.data?.error || e.message)
  } finally {
    loadingContent.value = false
  }
}

// ── Cerrar sesión ─────────────────────────────────────────────
async function logout() {
  try {
    await axios.post(`${BASE_URL}/execute`, { commands: 'logout' })
  } catch {}
  localStorage.removeItem('session')
  session.value = { active: false, username: '', partId: '' }
  router.push('/')
}
</script>

<style scoped>
.explorer-page {
  min-height: 100vh;
  background: #0d1117;
  color: #c9d1d9;
  font-family: monospace;
}

/* ── Navbar ── */
.navbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 20px;
  background: #161b22;
  border-bottom: 1px solid #30363d;
  position: sticky;
  top: 0;
  z-index: 10;
}

.nav-left  { display: flex; align-items: center; gap: 8px; }
.nav-right { display: flex; align-items: center; gap: 8px; }

.nav-logo {
  font-size: 15px;
  font-weight: 700;
  letter-spacing: 0.02em;
}
.bracket { color: #3fb950; }

.nav-sep        { color: #484f58; }
.nav-breadcrumb { font-size: 12px; color: #8b949e; }

.session-badge {
  font-size: 11px;
  color: #3fb950;
  padding: 3px 8px;
  background: rgba(63,185,80,0.1);
  border: 1px solid rgba(63,185,80,0.2);
  border-radius: 12px;
}

.btn-nav {
  padding: 5px 12px;
  background: #21262d;
  border: 1px solid #30363d;
  border-radius: 4px;
  color: #c9d1d9;
  font-size: 12px;
  font-family: monospace;
  cursor: pointer;
  transition: background 0.15s;
}
.btn-nav:hover    { background: #30363d; }
.btn-journal      { border-color: #388bfd; color: #58a6ff; }
.btn-logout       { border-color: #f85149; color: #f85149; }
.btn-logout:hover { background: rgba(248,81,73,0.1); }

/* ── Contenedor ── */
.container {
  max-width: 1000px;
  margin: 0 auto;
  padding: 28px 20px;
}

.section-title {
  font-size: 14px;
  color: #8b949e;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  margin-bottom: 20px;
  padding-bottom: 8px;
  border-bottom: 1px solid #21262d;
}

.loading {
  color: #8b949e;
  font-size: 13px;
  padding: 20px 0;
}

.empty {
  color: #484f58;
  font-size: 13px;
  padding: 20px 0;
}

.error-msg { color: #f85149; }

/* ── Back bar ── */
.back-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 20px;
}

.btn-back {
  background: none;
  border: none;
  color: #58a6ff;
  font-family: monospace;
  font-size: 12px;
  cursor: pointer;
  padding: 0;
}
.btn-back:hover { text-decoration: underline; }
.back-title { font-size: 13px; color: #8b949e; }

/* ── Grid de discos y particiones ── */
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 14px;
}

.card {
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 8px;
  padding: 18px 16px;
  cursor: pointer;
  transition: border-color 0.15s, background 0.15s;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.card:hover {
  border-color: #58a6ff;
  background: #1c2128;
}

.unmounted {
  opacity: 0.5;
  cursor: not-allowed;
}
.unmounted:hover {
  border-color: #30363d;
  background: #161b22;
}

.card-icon { font-size: 28px; }
.card-name { font-size: 14px; font-weight: 600; color: #e6edf3; }

.card-meta {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  font-size: 11px;
  color: #8b949e;
}

.badge {
  padding: 2px 6px;
  background: #21262d;
  border: 1px solid #30363d;
  border-radius: 10px;
  font-size: 10px;
}

.badge-ok  { border-color: #2ea043; color: #3fb950; background: rgba(63,185,80,0.08); }
.badge-off { border-color: #6e7681; color: #6e7681; }
.badge-id  { border-color: #388bfd; color: #58a6ff; }

.card-parts {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.part-tag {
  padding: 2px 6px;
  background: rgba(56,139,253,0.1);
  border: 1px solid rgba(56,139,253,0.2);
  border-radius: 10px;
  font-size: 10px;
  color: #58a6ff;
}

.empty-parts { font-size: 11px; color: #484f58; }

/* ── Path bar ── */
.path-bar {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 8px 12px;
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 6px;
  margin-bottom: 20px;
  font-size: 13px;
  flex-wrap: wrap;
}

.path-icon { font-size: 14px; margin-right: 4px; }

.path-seg {
  cursor: pointer;
  color: #58a6ff;
  transition: color 0.15s;
}
.path-seg:hover { color: #79c0ff; text-decoration: underline; }
.path-sep { color: #484f58; }

/* ── Grid de archivos ── */
.file-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(130px, 1fr));
  gap: 12px;
}

.file-card {
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 8px;
  padding: 14px 12px;
  cursor: pointer;
  transition: border-color 0.15s, background 0.15s;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  text-align: center;
}

.file-card:hover          { background: #1c2128; }
.is-folder:hover          { border-color: #e3b341; }
.is-file:hover            { border-color: #58a6ff; }

.file-icon { font-size: 28px; }
.file-name {
  font-size: 12px;
  color: #e6edf3;
  word-break: break-all;
  line-height: 1.3;
}

.file-meta {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
  justify-content: center;
}

.perm {
  font-size: 10px;
  color: #8b949e;
  padding: 1px 5px;
  background: #21262d;
  border-radius: 4px;
}

.size {
  font-size: 10px;
  color: #484f58;
}

/* ── Visor de archivo ── */
.file-viewer {
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 8px;
  overflow: hidden;
}

.viewer-header {
  padding: 10px 16px;
  background: #1c2128;
  border-bottom: 1px solid #30363d;
  font-size: 13px;
  color: #8b949e;
}

.viewer-filename { color: #58a6ff; }

.viewer-content {
  padding: 16px;
  font-family: 'Fira Code', monospace;
  font-size: 13px;
  line-height: 1.6;
  color: #c9d1d9;
  white-space: pre-wrap;
  word-break: break-word;
  min-height: 200px;
  max-height: 70vh;
  overflow-y: auto;
}
</style>