import { loadDocument } from 'pdfium-native';

// open from file path or Buffer
const doc = await loadDocument('invoice.pdf');

console.log(`Pages: ${doc.pageCount}`);
console.log(`Title: ${doc.metadata.title}`);
console.log(`Author: ${doc.metadata.author}`);

// extract text from every page
for await (const page of doc.pages()) {
  console.log(`\n--- Page ${page.number + 1} (${page.width} × ${page.height} pts) ---`);
  console.log(page.getText());
  page.close();
}

doc.destroy();
