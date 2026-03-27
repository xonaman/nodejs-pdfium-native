import { loadDocument, PDFiumPasswordError } from '@xonaman/pdfium-native';

const doc = await loadDocument('report.pdf');
const page = await doc.getPage(0);

// search for text (case-insensitive by default)
const matches = page.search('invoice');
console.log(`Found ${matches.length} match(es) for "invoice"`);
for (const match of matches) {
  console.log(`  "${match.text}" at ${match.rects.length} rect(s)`);
  for (const r of match.rects) {
    console.log(`    (${r.left}, ${r.top}) → (${r.right}, ${r.bottom})`);
  }
}

// case-sensitive search
const exact = page.search('Invoice', { caseSensitive: true });
console.log(`\nCase-sensitive: ${exact.length} match(es)`);

page.close();

// bookmarks
const bookmarks = doc.getBookmarks();
const printBookmarks = (items, indent = 0) => {
  for (const b of items) {
    console.log(`${'  '.repeat(indent)}▸ ${b.title} (page ${b.pageIndex})`);
    if (b.children.length) printBookmarks(b.children, indent + 1);
  }
};
if (bookmarks.length) {
  console.log('\nBookmarks:');
  printBookmarks(bookmarks);
}

doc.destroy();

// error handling with typed errors
try {
  await loadDocument('encrypted.pdf');
} catch (err) {
  if (err instanceof PDFiumPasswordError) {
    console.log('\nDocument is password-protected');
    // retry with password
    const doc2 = await loadDocument('encrypted.pdf', 'secret');
    console.log(`Opened with password, ${doc2.pageCount} page(s)`);
    doc2.destroy();
  }
}
