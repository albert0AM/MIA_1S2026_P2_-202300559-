<template>
  <div class="login-page">
    <div class="login-card">

      <div class="login-header">
        <div class="logo">
          <span class="logo-bracket">[</span>
          <span class="logo-text">ExtreamFS</span>
          <span class="logo-bracket">]</span>
        </div>
        <p class="subtitle">Iniciar sesión en una partición</p>
      </div>

      <form @submit.prevent="handleLogin" class="login-form">

        <div class="field">
          <label>ID Partición</label>
          <input
            v-model="form.partId"
            type="text"
            placeholder="ej: 591A"
            autocomplete="off"
            spellcheck="false"
            :class="{ 'input-error': errors.partId }"
          />
          <span class="field-error" v-if="errors.partId">{{ errors.partId }}</span>
        </div>

        <div class="field">
          <label>Usuario</label>
          <input
            v-model="form.username"
            type="text"
            placeholder="ej: root"
            autocomplete="off"
            spellcheck="false"
            :class="{ 'input-error': errors.username }"
          />
          <span class="field-error" v-if="errors.username">{{ errors.username }}</span>
        </div>

        <div class="field">
          <label>Contraseña</label>
          <input
            v-model="form.password"
            type="password"
            placeholder="••••••••"
            autocomplete="off"
            :class="{ 'input-error': errors.password }"
          />
          <span class="field-error" v-if="errors.password">{{ errors.password }}</span>
        </div>

        <div v-if="errorMsg" class="alert-error">
          {{ errorMsg }}
        </div>

        <button type="submit" class="btn-login" :disabled="loading">
          <span v-if="loading" class="spinner"></span>
          <span>{{ loading ? 'Iniciando...' : 'Iniciar Sesión' }}</span>
        </button>

      </form>

      <div class="login-footer">
        <button class="btn-back" @click="$router.push('/')">
          ← Volver a la terminal
        </button>
      </div>

    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import axios from 'axios'
import BASE_URL from './config.js'

const router = useRouter()

const form = ref({
  partId:   '',
  username: '',
  password: ''
})

const errors  = ref({})
const errorMsg = ref('')
const loading  = ref(false)

function validate() {
  errors.value = {}
  if (!form.value.partId.trim())   errors.value.partId   = 'Campo requerido'
  if (!form.value.username.trim()) errors.value.username = 'Campo requerido'
  if (!form.value.password.trim()) errors.value.password = 'Campo requerido'
  return Object.keys(errors.value).length === 0
}

async function handleLogin() {
  errorMsg.value = ''
  if (!validate()) return

  loading.value = true
  try {
    const cmd = `login -id=${form.value.partId} -user=${form.value.username} -pass=${form.value.password}`
    const res = await axios.post(`${BASE_URL}/execute`, { commands: cmd })
    const output = res.data.output || ''

    if (output.includes('SUCCESS')) {
      // Guardar sesión en localStorage para que la use el explorador
      localStorage.setItem('session', JSON.stringify({
        active:   true,
        partId:   form.value.partId,
        username: form.value.username
      }))
      router.push('/explorer')
    } else {
      errorMsg.value = output.includes('ERROR')
        ? output.split('\n').find(l => l.includes('ERROR')) || 'Credenciales incorrectas'
        : 'No se pudo iniciar sesión'
    }
  } catch {
    errorMsg.value = 'No se pudo conectar con el servidor'
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #0d1117;
  background-image:
    radial-gradient(ellipse at 20% 50%, rgba(35, 134, 54, 0.08) 0%, transparent 60%),
    radial-gradient(ellipse at 80% 20%, rgba(88, 166, 255, 0.06) 0%, transparent 50%);
}

.login-card {
  width: 100%;
  max-width: 400px;
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 8px;
  padding: 36px 32px;
  box-shadow: 0 16px 48px rgba(0, 0, 0, 0.4);
}

.login-header {
  text-align: center;
  margin-bottom: 28px;
}

.logo {
  font-family: 'Fira Code', 'Courier New', monospace;
  font-size: 22px;
  font-weight: 700;
  margin-bottom: 8px;
}

.logo-bracket { color: #3fb950; }
.logo-text    { color: #c9d1d9; margin: 0 4px; }

.subtitle {
  font-size: 12px;
  color: #8b949e;
  font-family: monospace;
}

.login-form {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 5px;
}

.field label {
  font-size: 12px;
  color: #8b949e;
  font-family: monospace;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.field input {
  padding: 9px 12px;
  background: #0d1117;
  border: 1px solid #30363d;
  border-radius: 6px;
  color: #c9d1d9;
  font-family: 'Fira Code', monospace;
  font-size: 13px;
  outline: none;
  transition: border-color 0.15s;
}

.field input:focus {
  border-color: #388bfd;
}

.field input.input-error {
  border-color: #f85149;
}

.field-error {
  font-size: 11px;
  color: #f85149;
  font-family: monospace;
}

.alert-error {
  padding: 10px 12px;
  background: rgba(248, 81, 73, 0.1);
  border: 1px solid rgba(248, 81, 73, 0.3);
  border-radius: 6px;
  color: #f85149;
  font-size: 12px;
  font-family: monospace;
}

.btn-login {
  margin-top: 4px;
  padding: 10px;
  background: #238636;
  border: 1px solid #2ea043;
  border-radius: 6px;
  color: #fff;
  font-family: monospace;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  transition: background 0.15s;
}

.btn-login:hover:not(:disabled) {
  background: #2ea043;
}

.btn-login:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.spinner {
  width: 14px;
  height: 14px;
  border: 2px solid rgba(255,255,255,0.3);
  border-top-color: #fff;
  border-radius: 50%;
  animation: spin 0.7s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.login-footer {
  margin-top: 20px;
  text-align: center;
}

.btn-back {
  background: none;
  border: none;
  color: #8b949e;
  font-family: monospace;
  font-size: 12px;
  cursor: pointer;
  transition: color 0.15s;
}

.btn-back:hover {
  color: #c9d1d9;
}
</style>