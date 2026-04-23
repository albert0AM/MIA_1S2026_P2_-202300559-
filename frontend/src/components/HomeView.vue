<template>
  <div class="app">
    <div class="toolbar">
      <input id="file-input" type="file" accept=".mia,.txt" @change="loadFile"/>
      <label for="file-input">Elegir archivo</label>
      <span id="file-name">{{ fileName }}</span>
      <button id="btn-ejecutar" @click="execute" :disabled="loading">
        {{ loading ? 'Ejecutando...' : 'Ejecutar' }}
      </button>
      <button @click="clear">Limpiar</button>
      <button v-if="!session.active" @click="goToLogin" class="btn-login">
        Iniciar Sesión
      </button>
      <template v-if="session.active">
        <div class="session-badge">● {{ session.username }} — {{ session.partId }}</div>
        <button @click="goToExplorer" class="btn-explorer">Explorador</button>
        <button @click="doLogout" class="btn-logout">Cerrar Sesión</button>
      </template>
    </div>

    <div class="panel">
      <div class="panel-label">Entrada:</div>
      <div class="editor">
        <div class="lines" ref="lnEntrada">{{ inputLineNumbers }}</div>
        <textarea
          id="entrada"
          v-model="input"
          placeholder="# Escribe tus comandos aquí..."
          spellcheck="false"
          ref="textarea"
          @scroll="syncScroll"
          @keydown.tab.prevent="insertTab"
        ></textarea>
      </div>
    </div>

    <div class="panel">
      <div class="panel-label">Salida:</div>
      <div class="editor">
        <div class="lines" ref="lnSalida">{{ outputLineNumbers }}</div>
        <div id="salida" :class="outputClass" v-html="formattedOutput"></div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import axios from 'axios'

const router   = useRouter()
const input    = ref('')
const output   = ref('# Acá se verán todos los mensajes de la ejecución')
const loading  = ref(false)
const fileName = ref('No se eligió archivo')
const textarea = ref(null)
const lnEntrada = ref(null)
const lnSalida = ref(null)
const session  = ref({ active: false, username: '', partId: '' })

onMounted(() => {
  const saved = localStorage.getItem('session')
  if (saved) session.value = JSON.parse(saved)
})

// Genera los números con saltos de línea reales
const inputLineNumbers = computed(() => {
  const n = input.value.split('\n').length
  return Array.from({ length: n }, (_, i) => i + 1).join('\n')
})

const outputLineNumbers = computed(() => {
  const n = output.value.split('\n').length
  return Array.from({ length: n }, (_, i) => i + 1).join('\n')
})

const outputClass = computed(() => {
  if (output.value.includes('ERROR') && !output.value.includes('SUCCESS')) return 'err'
  if (output.value.includes('SUCCESS')) return 'ok'
  return ''
})

const formattedOutput = computed(() => {
  return output.value
    .split('\n')
    .map(line => {
      if (line.trimStart().startsWith('SUCCESS'))
        return `<span style="color:#3fb950">${escapeHtml(line)}</span>`
      if (line.trimStart().startsWith('ERROR'))
        return `<span style="color:#f85149">${escapeHtml(line)}</span>`
      return `<span>${escapeHtml(line)}</span>`
    })
    .join('\n')
})

function escapeHtml(str) {
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

function syncScroll() {
  if (lnEntrada.value && textarea.value) {
    lnEntrada.value.scrollTop = textarea.value.scrollTop
  }
}

function insertTab() {
  const s = textarea.value.selectionStart
  input.value = input.value.slice(0, s) + '  ' + input.value.slice(s)
  setTimeout(() => {
    textarea.value.selectionStart = s + 2
    textarea.value.selectionEnd   = s + 2
  }, 0)
}

function loadFile(e) {
  const file = e.target.files[0]
  if (!file) return
  fileName.value = file.name
  const reader = new FileReader()
  reader.onload = ev => { input.value = ev.target.result }
  reader.readAsText(file)
  e.target.value = ''
}

async function execute() {
  if (!input.value.trim()) return

  const lines = input.value.split('\n')
  const blocked = lines.filter(l => /^\s*(login|logout)\s*/i.test(l.replace(/#.*/,'')))
  if (blocked.length > 0) {
    output.value = 'ERROR: los comandos login y logout se manejan desde los botones de la interfaz gráfica.'
    return
  }

  loading.value = true
  try {
    const res = await axios.post('/execute', { commands: input.value })
    output.value = res.data.output.trim()
    detectSession(output.value)
  } catch (e) {
    output.value = 'ERROR: no se pudo conectar con el servidor'
  } finally {
    loading.value = false
  }
}

function detectSession(text) {
  const m = text.match(/SUCCESS: sesión iniciada\s+usuario : (\S+)\s+id\s+: (\S+)/)
  if (m) {
    const s = { active: true, username: m[1], partId: m[2] }
    session.value = s
    localStorage.setItem('session', JSON.stringify(s))
  }
  if (text.includes('SUCCESS: sesión cerrada')) {
    session.value = { active: false, username: '', partId: '' }
    localStorage.removeItem('session')
  }
}

function clear() {
  input.value  = ''
  output.value = '# Acá se verán todos los mensajes de la ejecución'
}

function goToLogin() {
  router.push('/login')
}

function goToExplorer() {
  router.push('/explorer')
}

async function doLogout() {
  try {
    await axios.post('/execute', { commands: 'logout' })
  } catch { /* ignorar error de red */ }
  session.value = { active: false, username: '', partId: '' }
  localStorage.removeItem('session')
}
</script>

<style scoped>
.app {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: #0d1117;
  color: #c9d1d9;
  font-family: monospace;
}

.toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 14px;
  background: #1c2128;
  border-bottom: 1px solid #30363d;
  flex-shrink: 0;
}

.toolbar button, .toolbar label {
  padding: 5px 14px;
  background: #21262d;
  border: 1px solid #30363d;
  border-radius: 4px;
  color: #c9d1d9;
  font-size: 12px;
  cursor: pointer;
}

#btn-ejecutar {
  background: #238636;
  border-color: #2ea043;
  color: #fff;
}

.btn-login {
  background: #0969da;
  border-color: #0860ca;
  color: #fff;
}

.btn-explorer {
  background: #6e40c9;
  border-color: #6035b5;
  color: #fff;
}

.btn-logout {
  background: #b62324;
  border-color: #a01f20;
  color: #fff;
}

#file-input { display: none; }
#file-name  { font-size: 11px; color: #8b949e; flex: 1; }

.panel {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
}

.panel-label {
  padding: 6px 14px;
  font-size: 11px;
  color: #8b949e;
  background: #1c2128;
  border-bottom: 1px solid #30363d;
  border-top: 1px solid #30363d;
}

.editor {
  display: flex;
  flex: 1;
  overflow: hidden; /* Evita que el panel entero haga scroll */
}

.lines {
  padding: 10px 8px;
  background: #161b22;
  color: #484f58;
  text-align: right;
  border-right: 1px solid #30363d;
  font-size: 13px;
  line-height: 1.6;
  min-width: 45px;
  white-space: pre; /* Mantiene el formato de salto de línea del computed */
  user-select: none;
}

textarea, #salida {
  flex: 1;
  padding: 10px;
  font-family: 'Fira Code', monospace;
  font-size: 13px;
  line-height: 1.6;
  border: none;
  outline: none;
  background: transparent;
  color: #c9d1d9;
  overflow-y: auto; /* El scroll solo ocurre aquí */
  resize: none;
}

#salida {
  white-space: pre-wrap;
}
</style>