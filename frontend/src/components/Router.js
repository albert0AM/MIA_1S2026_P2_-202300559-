import { createRouter, createWebHistory } from 'vue-router'
import HomeView       from './HomeView.vue'
import LoginView      from './LoginView.vue'
import ExplorerView   from './ExplorerView.vue'
import JournalingView from './JournalingView.vue'

const routes = [
  { path: '/',           component: HomeView       },
  { path: '/login',      component: LoginView      },
  { path: '/explorer',   component: ExplorerView   },
  { path: '/journaling', component: JournalingView }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

export default router

