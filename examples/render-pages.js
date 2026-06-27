import { loadDocument } from 'pdfium-native';

const doc = await loadDocument('document.pdf');
const page = await doc.getPage(0);

// render as PNG buffer (PNG is the default format)
const png = await page.render();
console.log(`PNG: ${png.length} bytes`);

// render as JPEG buffer
const jpeg = await page.render({ format: 'jpeg', quality: 90 });
console.log(`JPEG: ${jpeg.length} bytes`);

// render at 2× resolution (PNG)
const hiRes = await page.render({ scale: 2 });
console.log(`2× PNG: ${hiRes.length} bytes`);

// render with a specific size and write a PNG to file
await page.render({ width: 1200, height: 1600, output: 'page-1.png' });
console.log('Saved page-1.png');

// render a JPEG to file (format is required when the extension is .jpg)
await page.render({ format: 'jpeg', output: 'page-1.jpg' });
console.log('Saved page-1.jpg');

page.close();
doc.destroy();
