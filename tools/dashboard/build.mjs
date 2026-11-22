import { copyFile, mkdir } from "node:fs/promises";
import * as esbuild from "esbuild";

const watch = process.argv.includes("--watch");

const options = {
  entryPoints: ["src/main.ts"],
  bundle: true,
  minify: !watch,
  sourcemap: watch,
  target: ["chrome120", "firefox121", "safari17"],
  outdir: "dist/assets",
  loader: { ".woff2": "file", ".woff": "file" },
  assetNames: "[name]-[hash]",
  logLevel: "info",
};

await mkdir("dist", { recursive: true });
await copyFile("src/index.html", "dist/index.html");

if (watch) {
  const context = await esbuild.context(options);
  await context.watch();
} else {
  await esbuild.build(options);
}
