import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  server: {
    port: 5173,
    proxy: {
      '/execute':    'http://localhost:8080',
      '/health':     'http://localhost:8080',
      '/disks':      'http://localhost:8080',
      '/partitions': 'http://localhost:8080',
      '/ls':         'http://localhost:8080',
      '/cat':        'http://localhost:8080',
      '/journaling': 'http://localhost:8080'
    }
  }
})
