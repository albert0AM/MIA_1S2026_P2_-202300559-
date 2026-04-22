<template>
  <div class="journal-page">

    <!-- ── Navbar ── -->
    <nav class="navbar">
      <div class="nav-left">
        <span class="nav-logo">
          <span class="bracket">[</span>ExtreamFS<span class="bracket">]</span>
        </span>
        <span class="nav-sep">/</span>
        <span class="nav-breadcrumb">Journaling</span>
      </div>
      <div class="nav-right">
        <button class="btn-nav" @click="$router.push('/')">Terminal</button>
        <button class="btn-nav btn-explorer" @click="$router.push('/explorer')">Explorador</button>
      </div>
    </nav>

    <div class="container">

      <!-- ── Selector de partición ── -->
      <div class="selector-bar">
        <label>ID Partición</label>
        <input
          v-model="partId"
          type="text"
          placeholder="ej: 591A"
          spellcheck="false"
          @keydown.enter="loadJournal"
        />
        <button class="btn-load" @click="loadJournal" :disabled="loading">
          {{ loading ? 'Cargando...' : 'Ver Journal' }}
        </button>
      </div>

      <!-- ── Estado ── -->
      <div v-if="!loaded && !loading && !error" class="empty">
        Ingresa el ID de una partición EXT3 y presiona "Ver Journal".
      </div>

      <div v-if="loading" class="loading">
        <span class="spinner"></span>
        Leyendo entradas del journal...
      </div>

      <div v-if="error" class="alert-error">{{ error }}</div>

      <!-- ── Tabla de entradas ── -->
      <div v-if="loaded && entries.length === 0" class="empty">
        No hay entradas registradas en el journal de esta partición.
      </div>

      <div v-if="loaded && entries.length > 0" class="journal-wrapper">

        <div class="journal-header">
          <span class="journal-title">
            📋 Journal — {{ partId }}
          </span>
          <span class="journal-count">{{ entries.length }} entradas</span>
        </div>

        <div class="table-wrapper">
          <table class="journal-table">
            <thead>
              <tr>
                <th>#</th>
                <th>Operación</th>
                <th>Path</th>
                <th>Contenido</th>
                <th>Fecha</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="entry in entries" :key="entry.count">
                <td class="td-count">{{ entry.count }}</td>
                <td class="td-op">
                  <span class="op-badge" :class="opClass(entry.operacion)">
                    {{ entry.operacion }}
                  </span>
                </td>
                <td class="td-path">{{ entry.path || '—' }}</td>
                <td class="td-content">
                  <span v-if="entry.contenido" class="content-preview">
                    {{ truncate(entry.contenido, 40) }}
                  </span>
                  <span v-else class="empty-cell">—</span>
                </td>
                <td class="td-date">{{ entry.fecha }}</td>
              </tr>
            </tbody>
          </table>
        </div>

      </div>

    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import axios from 'axios'
import BASE_URL from './config.js'

const partId  = ref('')
const entries = ref([])
const loading = ref(false)
const loaded  = ref(false)
const error   = ref('')

async function loadJournal() {
  if (!partId.value.trim()) {
    error.value = 'Ingresa un ID de partición'
    return
  }

  error.value  = ''
  loaded.value = false
  loading.value = true
  entries.value = []

  try {
    const res = await axios.get(`${BASE_URL}/journaling`, {
      params: { id: partId.value.trim() }
    })

    // El backend devuelve un JSON array directamente como string
    let data = res.data
    if (typeof data === 'string') {
      data = JSON.parse(data)
    }

    if (Array.isArray(data)) {
      entries.value = data
      loaded.value  = true
    } else if (data.error) {
      error.value = data.error
    } else {
      error.value = 'Respuesta inesperada del servidor'
    }
  } catch (e) {
    if (e.response?.data) {
      const d = e.response.data
      error.value = typeof d === 'string' ? d : (d.error || 'Error al cargar el journal')
    } else {
      error.value = 'No se pudo conectar con el servidor'
    }
  } finally {
    loading.value = false
  }
}

// Color por tipo de operación
function opClass(op) {
  const map = {
    mkdir:  'op-mkdir',
    mkfile: 'op-mkfile',
    remove: 'op-remove',
    rename: 'op-rename',
    copy:   'op-copy',
    move:   'op-move',
    chown:  'op-chown',
    chmod:  'op-chmod',
  }
  return map[op?.toLowerCase()] || 'op-default'
}

function truncate(str, max) {
  if (!str) return ''
  return str.length > max ? str.slice(0, max) + '…' : str
}
</script>

<style scoped>
.journal-page {
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
}
.bracket        { color: #3fb950; }
.nav-sep        { color: #484f58; }
.nav-breadcrumb { font-size: 12px; color: #8b949e; }

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
.btn-explorer     { border-color: #e3b341; color: #e3b341; }

/* ── Contenedor ── */
.container {
  max-width: 1000px;
  margin: 0 auto;
  padding: 28px 20px;
}

/* ── Selector ── */
.selector-bar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 24px;
  flex-wrap: wrap;
}

.selector-bar label {
  font-size: 12px;
  color: #8b949e;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.selector-bar input {
  padding: 7px 12px;
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 6px;
  color: #c9d1d9;
  font-family: monospace;
  font-size: 13px;
  outline: none;
  width: 140px;
  transition: border-color 0.15s;
}
.selector-bar input:focus { border-color: #388bfd; }

.btn-load {
  padding: 7px 16px;
  background: #1f6feb;
  border: 1px solid #388bfd;
  border-radius: 6px;
  color: #fff;
  font-family: monospace;
  font-size: 13px;
  cursor: pointer;
  transition: background 0.15s;
}
.btn-load:hover:not(:disabled) { background: #388bfd; }
.btn-load:disabled { opacity: 0.5; cursor: not-allowed; }

/* ── Estados ── */
.loading {
  display: flex;
  align-items: center;
  gap: 10px;
  color: #8b949e;
  font-size: 13px;
  padding: 20px 0;
}

.spinner {
  width: 14px;
  height: 14px;
  border: 2px solid rgba(255,255,255,0.2);
  border-top-color: #58a6ff;
  border-radius: 50%;
  animation: spin 0.7s linear infinite;
}
@keyframes spin { to { transform: rotate(360deg); } }

.empty {
  color: #484f58;
  font-size: 13px;
  padding: 20px 0;
}

.alert-error {
  padding: 10px 14px;
  background: rgba(248, 81, 73, 0.1);
  border: 1px solid rgba(248, 81, 73, 0.3);
  border-radius: 6px;
  color: #f85149;
  font-size: 12px;
}

/* ── Journal wrapper ── */
.journal-wrapper {
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 8px;
  overflow: hidden;
}

.journal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  background: #1c2128;
  border-bottom: 1px solid #30363d;
}

.journal-title {
  font-size: 13px;
  color: #e6edf3;
  font-weight: 600;
}

.journal-count {
  font-size: 11px;
  color: #8b949e;
  padding: 2px 8px;
  background: #21262d;
  border: 1px solid #30363d;
  border-radius: 10px;
}

/* ── Tabla ── */
.table-wrapper {
  overflow-x: auto;
}

.journal-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}

.journal-table th {
  padding: 10px 14px;
  text-align: left;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: #8b949e;
  background: #161b22;
  border-bottom: 1px solid #30363d;
  white-space: nowrap;
}

.journal-table td {
  padding: 10px 14px;
  border-bottom: 1px solid #21262d;
  vertical-align: middle;
}

.journal-table tr:last-child td {
  border-bottom: none;
}

.journal-table tr:hover td {
  background: #1c2128;
}

.td-count { color: #484f58; width: 40px; }
.td-path  { color: #8b949e; font-family: 'Fira Code', monospace; }
.td-date  { color: #484f58; white-space: nowrap; }

.td-content .content-preview {
  color: #8b949e;
  font-family: 'Fira Code', monospace;
  font-size: 11px;
}
.empty-cell { color: #30363d; }

/* ── Badges de operación ── */
.op-badge {
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 600;
  border: 1px solid;
}

.op-mkdir  { color: #3fb950; border-color: rgba(63,185,80,0.3);  background: rgba(63,185,80,0.08);  }
.op-mkfile { color: #58a6ff; border-color: rgba(88,166,255,0.3); background: rgba(88,166,255,0.08); }
.op-remove { color: #f85149; border-color: rgba(248,81,73,0.3);  background: rgba(248,81,73,0.08);  }
.op-rename { color: #e3b341; border-color: rgba(227,179,65,0.3); background: rgba(227,179,65,0.08); }
.op-copy   { color: #a371f7; border-color: rgba(163,113,247,0.3);background: rgba(163,113,247,0.08);}
.op-move   { color: #ffa657; border-color: rgba(255,166,87,0.3); background: rgba(255,166,87,0.08); }
.op-chown  { color: #79c0ff; border-color: rgba(121,192,255,0.3);background: rgba(121,192,255,0.08);}
.op-chmod  { color: #d2a8ff; border-color: rgba(210,168,255,0.3);background: rgba(210,168,255,0.08);}
.op-default{ color: #8b949e; border-color: #30363d; background: #21262d; }
</style>