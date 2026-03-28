/**
 * Generate test fixture PDFs. Run once with: node scripts/generate-fixtures.mjs
 * Requires: npm install pdf-lib (dev only, not committed)
 */
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { PDFDocument, StandardFonts, rgb } from 'pdf-lib';

const __dirname = dirname(fileURLToPath(import.meta.url));
const fixturesDir = resolve(__dirname, '..', 'test', 'fixtures');
mkdirSync(fixturesDir, { recursive: true });

// --- PDF with metadata ---
async function createMetadataPdf() {
  const doc = await PDFDocument.create();
  doc.setTitle('Test Document Title');
  doc.setAuthor('Test Author');
  doc.setSubject('Test Subject');
  doc.setKeywords(['test', 'fixture', 'pdfium']);
  doc.setCreator('pdfium-native test');
  doc.setProducer('pdf-lib');
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Metadata test page', { x: 50, y: 700, size: 16, font });
  return doc.save();
}

// --- PDF with annotations ---
async function createAnnotationPdf() {
  const doc = await PDFDocument.create();
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Page with annotations', { x: 50, y: 700, size: 16, font });

  // add a text annotation (sticky note) via raw PDF dict
  const { PDFName, PDFString } = await import('pdf-lib');
  const annotDict = doc.context.obj({
    Type: 'Annot',
    Subtype: 'Text',
    Rect: [100, 600, 120, 620],
    Contents: PDFString.of('This is a sticky note'),
    C: [1, 0, 0], // red
    Open: true,
  });
  const annotRef = doc.context.register(annotDict);

  // add a highlight annotation
  const highlightDict = doc.context.obj({
    Type: 'Annot',
    Subtype: 'Highlight',
    Rect: [50, 695, 250, 720],
    Contents: PDFString.of('Highlighted text'),
    C: [1, 1, 0], // yellow
    QuadPoints: [50, 720, 250, 720, 50, 695, 250, 695],
  });
  const highlightRef = doc.context.register(highlightDict);

  // attach annotations to page
  const pageDict = page.node;
  pageDict.set(PDFName.of('Annots'), doc.context.obj([annotRef, highlightRef]));

  return doc.save();
}

// --- PDF with links ---
async function createLinkPdf() {
  const doc = await PDFDocument.create();
  const page1 = doc.addPage([612, 792]);
  const page2 = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);

  page1.drawText('Page 1 - click links below', { x: 50, y: 700, size: 16, font });
  page1.drawText('Go to page 2', { x: 50, y: 650, size: 12, font, color: rgb(0, 0, 1) });
  page1.drawText('Visit example.com', { x: 50, y: 620, size: 12, font, color: rgb(0, 0, 1) });

  page2.drawText('Page 2 - target', { x: 50, y: 700, size: 16, font });

  // create link annotations via raw PDF
  const { PDFName, PDFString } = await import('pdf-lib');

  // internal link to page 2
  const page2Ref = doc.getPages()[1].ref;
  const internalLink = doc.context.obj({
    Type: 'Annot',
    Subtype: 'Link',
    Rect: [50, 645, 200, 665],
    Dest: [page2Ref, 'Fit'],
    Border: [0, 0, 0],
  });
  const internalLinkRef = doc.context.register(internalLink);

  // external URI link
  const uriAction = doc.context.obj({
    Type: 'Action',
    S: 'URI',
    URI: PDFString.of('https://example.com'),
  });
  const uriActionRef = doc.context.register(uriAction);
  const externalLink = doc.context.obj({
    Type: 'Annot',
    Subtype: 'Link',
    Rect: [50, 615, 250, 635],
    A: uriActionRef,
    Border: [0, 0, 0],
  });
  const externalLinkRef = doc.context.register(externalLink);

  const page1Dict = page1.node;
  page1Dict.set(PDFName.of('Annots'), doc.context.obj([internalLinkRef, externalLinkRef]));

  return doc.save();
}

// --- PDF with bookmarks ---
async function createBookmarkPdf() {
  const doc = await PDFDocument.create();
  const font = await doc.embedFont(StandardFonts.Helvetica);

  const page1 = doc.addPage([612, 792]);
  page1.drawText('Chapter 1: Introduction', { x: 50, y: 700, size: 20, font });

  const page2 = doc.addPage([612, 792]);
  page2.drawText('Chapter 2: Getting Started', { x: 50, y: 700, size: 20, font });

  const page3 = doc.addPage([612, 792]);
  page3.drawText('Chapter 3: Advanced Topics', { x: 50, y: 700, size: 20, font });

  // build outline tree via raw PDF objects
  const { PDFName, PDFString } = await import('pdf-lib');

  const page1Ref = doc.getPages()[0].ref;
  const page2Ref = doc.getPages()[1].ref;
  const page3Ref = doc.getPages()[2].ref;

  // outline items (linked list)
  const item1 = doc.context.obj({
    Title: PDFString.of('Chapter 1'),
    Dest: [page1Ref, 'Fit'],
  });
  const item1Ref = doc.context.register(item1);

  const item2 = doc.context.obj({
    Title: PDFString.of('Chapter 2'),
    Dest: [page2Ref, 'Fit'],
  });
  const item2Ref = doc.context.register(item2);

  const item3 = doc.context.obj({
    Title: PDFString.of('Chapter 3'),
    Dest: [page3Ref, 'Fit'],
  });
  const item3Ref = doc.context.register(item3);

  // link siblings
  item1.set(PDFName.of('Next'), item2Ref);
  item2.set(PDFName.of('Prev'), item1Ref);
  item2.set(PDFName.of('Next'), item3Ref);
  item3.set(PDFName.of('Prev'), item2Ref);

  // outline root
  const outlines = doc.context.obj({
    Type: 'Outlines',
    First: item1Ref,
    Last: item3Ref,
    Count: 3,
  });
  const outlinesRef = doc.context.register(outlines);

  // set parent on items
  item1.set(PDFName.of('Parent'), outlinesRef);
  item2.set(PDFName.of('Parent'), outlinesRef);
  item3.set(PDFName.of('Parent'), outlinesRef);

  // attach to catalog
  doc.catalog.set(PDFName.of('Outlines'), outlinesRef);

  return doc.save();
}

// --- Minimal single-page PDF ---
async function createMinimalPdf() {
  const doc = await PDFDocument.create();
  doc.addPage([612, 792]);
  return doc.save();
}

// --- Two-page PDF with different sizes ---
async function createTwoPagePdf() {
  const doc = await PDFDocument.create();
  doc.addPage([612, 792]);
  doc.addPage([400, 600]);
  return doc.save();
}

// --- PDF with text content ---
async function createTextPdf() {
  const doc = await PDFDocument.create();
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Hello', { x: 100, y: 700, size: 12, font });
  return doc.save();
}

// Generate all fixtures
const fixtures = [
  ['metadata.pdf', createMetadataPdf],
  ['annotations.pdf', createAnnotationPdf],
  ['links.pdf', createLinkPdf],
  ['bookmarks.pdf', createBookmarkPdf],
  ['minimal.pdf', createMinimalPdf],
  ['two-page.pdf', createTwoPagePdf],
  ['text.pdf', createTextPdf],
];

for (const [name, generator] of fixtures) {
  const bytes = await generator();
  const path = resolve(fixturesDir, name);
  writeFileSync(path, bytes);
  console.log(`Created ${path} (${bytes.length} bytes)`);
}
