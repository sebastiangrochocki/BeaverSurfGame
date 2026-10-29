import { build } from "esbuild";
import { fileURLToPath } from "node:url";
await build({
  entryPoints: ["ui.jsx"],
  bundle: true,
  minify: true,
  outfile: "workspace.js",
  define: { "process.env.NODE_ENV": '"production"' },
  plugins: [{
    name: "consistent-theme-icons",
    setup(build) {
      build.onResolve({filter: /^\.\/icons\.js$/}, args => {
        if (args.importer.includes("/@radix-ui/themes/"))
          return {path:fileURLToPath(new URL("./theme-icons.jsx",import.meta.url))};
      });
    },
  }],
});
