import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  timeout: 60000,
  testDir: "./tests",
  testMatch: "*.spec.js",
  use: { viewport: { width: 1440, height: 1000 } },
  projects: [
    { name: "chromium", use: { ...devices["Desktop Chrome"] } },
    { name: "firefox", use: { ...devices["Desktop Firefox"] } },
    { name: "webkit", use: { ...devices["Desktop Safari"] } },
  ],
});
