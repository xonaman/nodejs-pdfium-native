import { cpSync, existsSync, readdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';

const root = join(import.meta.dirname, '..');
const outDir = join(root, 'build', 'Release');
const nodeFile = join(outDir, 'pdfium.node');

if (!existsSync(nodeFile)) {
  console.error('pdfium.node not found — run node-gyp rebuild first');
  process.exit(1);
}

// on Windows, binding.gyp copies the DLL via "copies" — nothing else to do
if (process.platform === 'win32') {
  const dll = join(outDir, 'pdfium.dll');
  if (existsSync(dll)) {
    console.log('pdfium.dll already in build/Release/ (copied by node-gyp).');
  } else {
    console.error('pdfium.dll not found in build/Release/ — check binding.gyp copies.');
    process.exit(1);
  }
  console.log('Bundle complete.');
  process.exit(0);
}

// macOS / Linux: copy shared library next to pdfium.node
const libDir = join(root, 'deps', 'pdfium', 'lib');
const libs = readdirSync(libDir).filter((f) => f.endsWith('.dylib') || f.endsWith('.so'));
if (libs.length === 0) {
  console.error('No shared library found in deps/pdfium/lib/');
  process.exit(1);
}

for (const lib of libs) {
  const src = join(libDir, lib);
  const dst = join(outDir, lib);
  cpSync(src, dst);
  console.log(`Copied ${lib} → build/Release/`);
}

// on macOS, fix the install name so pdfium.node looks for the dylib next to itself
if (process.platform === 'darwin') {
  const dylib = libs.find((f) => f.endsWith('.dylib'));
  if (dylib) {
    // change the dylib's own install name
    execFileSync('install_name_tool', ['-id', `@loader_path/${dylib}`, join(outDir, dylib)], {
      stdio: 'inherit',
    });
    // change the reference inside pdfium.node
    const oldName = execFileSync('otool', ['-L', nodeFile])
      .toString()
      .split('\n')
      .find((line) => line.includes(dylib))
      ?.trim()
      .split(' ')[0];
    if (oldName) {
      execFileSync('install_name_tool', ['-change', oldName, `@loader_path/${dylib}`, nodeFile], {
        stdio: 'inherit',
      });
      console.log(`Fixed install name: ${oldName} → @loader_path/${dylib}`);
    }
  }
}

console.log('Bundle complete.');
