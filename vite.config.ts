/// <reference types="vitest/config" />
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vuetify from 'vite-plugin-vuetify'

export default defineConfig({
  // Relative assets work in both the native WebView resource provider and web preview.
  base: './',
  plugins: [vue(), vuetify({ autoImport: true })],
  test: {
    environment: './src/testing/environment.ts',
    include: ['src/**/*.test.ts'],
  },
})
