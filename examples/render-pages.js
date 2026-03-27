import { loadDocument } from '@xonaman/pdfium-native';

const doc = await loadDocument('document.pdf');
const page = await doc.getPage(0);

// render as JPEG buffer (default)
const jpeg = await page.render();
console.log(`JPEG: ${jpeg.length} bytes`);

// render as PNG buffer
const png = await page.render({ format: 'png' });
console.log(`PNG: ${png.length} bytes`);

// render at 2× resolution
const hiRes = await page.render({ scale: 2 });
console.log(`2× JPEG: ${hiRes.length} bytes`);

// render with a specific size and write to file
await page.render({ width: 1200, height: 1600, output: 'page-1.jpg' });
console.log('Saved page-1.jpg');

// render as PNG to file
await page.render({ format: 'png', output: 'page-1.png' });
console.log('Saved page-1.png');

page.close();
doc.destroy();
