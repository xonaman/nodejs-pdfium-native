/**
 * Generate test fixture PDFs. Run once with: node scripts/generate-fixtures.mjs
 * Requires: npm install pdf-lib (dev only, not committed)
 */
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Buffer } from 'node:buffer';
import { PDFDocument, StandardFonts, rgb, degrees } from 'pdf-lib';

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

// --- PDF with rich annotation metadata (author, subject, dates, flags) ---
async function createRichAnnotationPdf() {
  const doc = await PDFDocument.create();
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Page with rich annotations', { x: 50, y: 700, size: 16, font });

  const { PDFName, PDFString, PDFHexString } = await import('pdf-lib');

  const annot = doc.context.obj({
    Type: 'Annot',
    Subtype: 'Text',
    Rect: [100, 600, 120, 620],
    Contents: PDFHexString.fromText('Test note'),
    T: PDFHexString.fromText('John Doe'),
    Subj: PDFHexString.fromText('Review Comment'),
    CreationDate: PDFString.of('D:20250101120000Z'),
    M: PDFString.of('D:20250102120000Z'),
    C: [1, 0, 0],
    F: 4, // print flag
  });
  const annotRef = doc.context.register(annot);

  page.node.set(PDFName.of('Annots'), doc.context.obj([annotRef]));
  return doc.save();
}

// --- PDF with a rotated page ---
async function createRotatedPagePdf() {
  const doc = await PDFDocument.create();
  const font = await doc.embedFont(StandardFonts.Helvetica);
  const page = doc.addPage([612, 792]);
  page.drawText('This page is rotated 90° clockwise', { x: 50, y: 700, size: 16, font });
  page.setRotation(degrees(90));
  return doc.save();
}

// --- PDF with CropBox and TrimBox ---
async function createPageBoxesPdf() {
  const doc = await PDFDocument.create();
  const { PDFName } = await import('pdf-lib');
  const font = await doc.embedFont(StandardFonts.Helvetica);
  const page = doc.addPage([612, 792]);
  page.drawText('Page with CropBox and TrimBox', { x: 80, y: 700, size: 14, font });
  page.node.set(PDFName.of('CropBox'), doc.context.obj([50, 50, 562, 742]));
  page.node.set(PDFName.of('TrimBox'), doc.context.obj([72, 72, 540, 720]));
  return doc.save();
}

// --- Tagged PDF with language ---
async function createTaggedPdf() {
  const doc = await PDFDocument.create();
  const { PDFName, PDFString } = await import('pdf-lib');
  const font = await doc.embedFont(StandardFonts.Helvetica);
  const page = doc.addPage([612, 792]);
  page.drawText('Tagged PDF with language', { x: 50, y: 700, size: 16, font });
  const markInfo = doc.context.obj({ Marked: true });
  doc.catalog.set(PDFName.of('MarkInfo'), markInfo);
  doc.catalog.set(PDFName.of('Lang'), PDFString.of('en-US'));
  return doc.save();
}

// --- PDF with an embedded image ---
async function createImagePdf() {
  const doc = await PDFDocument.create();
  // create a minimal 2x2 red PNG manually (smallest valid PNG)
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  // IHDR chunk: 2x2, 8-bit RGB
  const ihdr = Buffer.alloc(25);
  ihdr.writeUInt32BE(13, 0); // length
  ihdr.write('IHDR', 4);
  ihdr.writeUInt32BE(2, 8); // width
  ihdr.writeUInt32BE(2, 12); // height
  ihdr[16] = 8; // bit depth
  ihdr[17] = 2; // color type RGB
  const { crc32, deflateSync } = await import('node:zlib');
  ihdr.writeUInt32BE(crc32(ihdr.subarray(4, 21)) >>> 0, 21);
  // IDAT chunk: zlib-compressed scanlines (filter byte + 3 bytes per pixel × 2 pixels × 2 rows)
  const raw = Buffer.from([
    0,
    255,
    0,
    0,
    255,
    0,
    0, // row1: filter=0, red, red
    0,
    0,
    0,
    255,
    0,
    0,
    255, // row2: filter=0, blue, blue
  ]);
  const compressed = deflateSync(raw);
  const idat = Buffer.alloc(compressed.length + 12);
  idat.writeUInt32BE(compressed.length, 0);
  idat.write('IDAT', 4);
  compressed.copy(idat, 8);
  idat.writeUInt32BE(crc32(idat.subarray(4, 8 + compressed.length)) >>> 0, 8 + compressed.length);
  // IEND chunk
  const iend = Buffer.from([0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130]);
  const pngData = Buffer.concat([signature, ihdr, idat, iend]);

  const img = await doc.embedPng(pngData);
  const page = doc.addPage([612, 792]);
  page.drawImage(img, { x: 50, y: 600, width: 100, height: 100 });
  return doc.save();
}

// --- PDF with rich bookmarks (XYZ dest, open/closed, nested, URI) ---
async function createRichBookmarkPdf() {
  const doc = await PDFDocument.create();
  const font = await doc.embedFont(StandardFonts.Helvetica);

  const page1 = doc.addPage([612, 792]);
  page1.drawText('Chapter 1: Introduction', { x: 50, y: 700, size: 20, font });
  const page2 = doc.addPage([612, 792]);
  page2.drawText('Section 1.1: Background', { x: 70, y: 700, size: 16, font });
  const page3 = doc.addPage([612, 792]);
  page3.drawText('Chapter 2: Resources', { x: 50, y: 700, size: 20, font });

  const { PDFName, PDFString, PDFNumber } = await import('pdf-lib');

  const page1Ref = doc.getPages()[0].ref;
  const page2Ref = doc.getPages()[1].ref;
  const page3Ref = doc.getPages()[2].ref;

  // child bookmark for chapter 1 (section 1.1)
  const child1 = doc.context.obj({
    Title: PDFString.of('Section 1.1'),
    Dest: doc.context.obj([
      page2Ref,
      PDFName.of('XYZ'),
      PDFNumber.of(70),
      PDFNumber.of(700),
      PDFNumber.of(1.5),
    ]),
  });
  const child1Ref = doc.context.register(child1);

  // chapter 1 (open, with child, XYZ dest)
  const item1 = doc.context.obj({
    Title: PDFString.of('Chapter 1'),
    Dest: doc.context.obj([
      page1Ref,
      PDFName.of('XYZ'),
      PDFNumber.of(50),
      PDFNumber.of(700),
      PDFNumber.of(1),
    ]),
    First: child1Ref,
    Last: child1Ref,
    Count: 1, // positive = open
  });
  const item1Ref = doc.context.register(item1);

  // chapter 2 (closed, URI action)
  const uriAction = doc.context.obj({
    Type: 'Action',
    S: 'URI',
    URI: PDFString.of('https://example.com/resources'),
  });
  const uriActionRef = doc.context.register(uriAction);
  const item2 = doc.context.obj({
    Title: PDFString.of('Resources (link)'),
    A: uriActionRef,
  });
  const item2Ref = doc.context.register(item2);

  // chapter 3 (Fit dest)
  const item3 = doc.context.obj({
    Title: PDFString.of('Chapter 2'),
    Dest: doc.context.obj([page3Ref, PDFName.of('Fit')]),
  });
  const item3Ref = doc.context.register(item3);

  // link siblings
  item1.set(PDFName.of('Next'), item2Ref);
  item2.set(PDFName.of('Prev'), item1Ref);
  item2.set(PDFName.of('Next'), item3Ref);
  item3.set(PDFName.of('Prev'), item2Ref);

  // child parent
  child1.set(PDFName.of('Parent'), item1Ref);

  // outline root
  const outlines = doc.context.obj({
    Type: 'Outlines',
    First: item1Ref,
    Last: item3Ref,
    Count: 4,
  });
  const outlinesRef = doc.context.register(outlines);

  item1.set(PDFName.of('Parent'), outlinesRef);
  item2.set(PDFName.of('Parent'), outlinesRef);
  item3.set(PDFName.of('Parent'), outlinesRef);

  doc.catalog.set(PDFName.of('Outlines'), outlinesRef);

  return doc.save();
}

// --- PDF with annotations that have border and interior color ---
async function createBorderAnnotationPdf() {
  const doc = await PDFDocument.create();
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Annotations with borders and fill', { x: 50, y: 700, size: 16, font });

  const { PDFName, PDFString } = await import('pdf-lib');

  // square annotation with interior color and border
  const squareAnnot = doc.context.obj({
    Type: 'Annot',
    Subtype: 'Square',
    Rect: [100, 500, 250, 600],
    Contents: PDFString.of('Filled square'),
    C: [0, 0, 1], // blue stroke
    IC: [1, 1, 0], // yellow fill
    Border: [5, 5, 3], // hRadius, vRadius, width
  });
  const squareRef = doc.context.register(squareAnnot);

  // circle annotation with interior color
  const circleAnnot = doc.context.obj({
    Type: 'Annot',
    Subtype: 'Circle',
    Rect: [300, 500, 450, 600],
    Contents: PDFString.of('Filled circle'),
    C: [1, 0, 0], // red stroke
    IC: [0, 1, 0], // green fill
    Border: [0, 0, 2],
  });
  const circleRef = doc.context.register(circleAnnot);

  page.node.set(PDFName.of('Annots'), doc.context.obj([squareRef, circleRef]));
  return doc.save();
}

// --- ZUGFeRD / Factur-X PDF/A-3 with an embedded invoice XML ---
// The structured invoice (factur-x.xml) is embedded in the /EmbeddedFiles name
// tree; a second plain-text attachment exercises multi-attachment enumeration.
const FACTUR_X_XML = `<?xml version="1.0" encoding="UTF-8"?>
<rsm:CrossIndustryInvoice xmlns:rsm="urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100" xmlns:ram="urn:un:unece:uncefact:data:standard:ReusableAggregateBusinessInformationEntity:100">
  <rsm:ExchangedDocumentContext>
    <ram:GuidelineSpecifiedDocumentContextParameter>
      <ram:ID>urn:cen.eu:en16931:2017</ram:ID>
    </ram:GuidelineSpecifiedDocumentContextParameter>
  </rsm:ExchangedDocumentContext>
  <rsm:ExchangedDocument>
    <ram:ID>RE-2025-0001</ram:ID>
    <ram:TypeCode>380</ram:TypeCode>
    <ram:IssueDateTime>
      <ram:DateTimeString format="102">20250101</ram:DateTimeString>
    </ram:IssueDateTime>
  </rsm:ExchangedDocument>
</rsm:CrossIndustryInvoice>
`;

async function createEInvoicePdf() {
  const { AFRelationship } = await import('pdf-lib');
  const doc = await PDFDocument.create();
  doc.setTitle('ZUGFeRD / Factur-X invoice');
  doc.setProducer('pdfium-native test');
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Invoice RE-2025-0001', { x: 50, y: 700, size: 16, font });
  page.drawText('Structured data embedded as factur-x.xml', { x: 50, y: 670, size: 11, font });

  await doc.attach(Buffer.from(FACTUR_X_XML, 'utf8'), 'factur-x.xml', {
    mimeType: 'text/xml',
    description: 'Factur-X invoice',
    creationDate: new Date('2025-01-01T12:00:00Z'),
    modificationDate: new Date('2025-01-01T12:00:00Z'),
    afRelationship: AFRelationship.Alternative,
  });

  await doc.attach(Buffer.from('Human-readable notes.\n', 'utf8'), 'notes.txt', {
    mimeType: 'text/plain',
    description: 'Supplementary notes',
    creationDate: new Date('2025-01-01T12:00:00Z'),
    modificationDate: new Date('2025-01-01T12:00:00Z'),
    afRelationship: AFRelationship.Supplement,
  });

  return doc.save();
}

// --- PDF exercising positioned text extraction ---
// Three runs on one page, each drawn at a known baseline origin: two font
// sizes, a regular/bold font switch, and a 45-degree rotated run. That is
// enough to pin down every geometry field getCharacters() reports without
// depending on glyph outlines.
async function createPositionedTextPdf() {
  const doc = await PDFDocument.create();
  const page = doc.addPage([400, 300]);
  const regular = await doc.embedFont(StandardFonts.Helvetica);
  const bold = await doc.embedFont(StandardFonts.HelveticaBold);

  page.drawText('AB', { x: 50, y: 250, size: 24, font: regular });
  page.drawText('CD', { x: 50, y: 200, size: 12, font: bold });
  page.drawText('EF', { x: 200, y: 100, size: 18, font: regular, rotate: degrees(45) });

  return doc.save();
}

// --- PDF with two AcroForm signature fields ---
// PDFium finds signatures by walking Catalog -> /AcroForm -> /Fields and
// keeping the entries whose /FT is /Sig, so the fields have to be built by
// hand — pdf-lib cannot create signature fields.
//
// The /Contents blobs and /ByteRange offsets below are FAKE: they are fixed
// byte patterns, not a real PKCS#7 structure, and the offsets do not describe
// this file. That is deliberate and sufficient, because PDFium performs no
// cryptographic verification — it hands back whatever the dictionary says. The
// fixture proves the extraction path, never signature validity.
const SIG_CONTENTS_HEX = '308006092a864886f70d010702a0803080020101';
const CERT_SIG_CONTENTS_HEX = '3082010a0282010100c0ffee';

async function createSignedPdf() {
  const doc = await PDFDocument.create();
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Signed document', { x: 50, y: 700, size: 16, font });

  const { PDFName, PDFString, PDFHexString } = await import('pdf-lib');

  // ordinary approval signature — no /Reference, so no DocMDP permission
  const sigValueRef = doc.context.register(
    doc.context.obj({
      Type: 'Sig',
      Filter: 'Adobe.PPKLite',
      SubFilter: 'adbe.pkcs7.detached',
      ByteRange: [0, 840, 1560, 1234],
      Contents: PDFHexString.of(SIG_CONTENTS_HEX),
      Reason: PDFString.of('I approve this document'),
      M: PDFString.of("D:20250101120000+01'00'"),
      Name: PDFString.of('Test Signer'),
    }),
  );

  const sigFieldRef = doc.context.register(
    doc.context.obj({
      FT: 'Sig',
      T: PDFString.of('Signature1'),
      V: sigValueRef,
      Type: 'Annot',
      Subtype: 'Widget',
      Rect: [50, 600, 250, 650],
      F: 4,
      P: page.ref,
    }),
  );

  // certification signature — /Reference -> /DocMDP -> /TransformParams /P 2
  const certValueRef = doc.context.register(
    doc.context.obj({
      Type: 'Sig',
      Filter: 'Adobe.PPKLite',
      SubFilter: 'ETSI.CAdES.detached',
      ByteRange: [0, 200, 900, 300],
      Contents: PDFHexString.of(CERT_SIG_CONTENTS_HEX),
      M: PDFString.of('D:20250202093000Z'),
      Reference: [
        {
          Type: 'SigRef',
          TransformMethod: 'DocMDP',
          TransformParams: { Type: 'TransformParams', P: 2, V: '1.2' },
        },
      ],
    }),
  );

  const certFieldRef = doc.context.register(
    doc.context.obj({
      FT: 'Sig',
      T: PDFString.of('Certification'),
      V: certValueRef,
      Type: 'Annot',
      Subtype: 'Widget',
      Rect: [50, 520, 250, 570],
      F: 4,
      P: page.ref,
    }),
  );

  doc.catalog.set(
    PDFName.of('AcroForm'),
    doc.context.obj({ Fields: [sigFieldRef, certFieldRef], SigFlags: 3 }),
  );
  page.node.set(PDFName.of('Annots'), doc.context.obj([sigFieldRef, certFieldRef]));

  // pdf-lib would otherwise re-read the AcroForm to regenerate widget
  // appearances, which these hand-built signature fields do not need
  return doc.save({ updateFieldAppearances: false });
}

// --- PDF with a page-level FileAttachment annotation ---
// PDFium exposes "paperclip" file attachments as /FileAttachment annotations
// whose embedded file lives on the annotation itself, NOT in the
// /EmbeddedFiles name tree (that is the document-level attachment API). A plain
// Text annotation precedes it so tests can exercise stable annotation indices
// and the "not a file attachment" error path.
const ATTACHMENT_REPORT_TXT = 'Quarterly report attachment.\nSecond line.\n';

async function createFileAttachmentAnnotationPdf() {
  const doc = await PDFDocument.create();
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Page with a file-attachment annotation', { x: 50, y: 700, size: 14, font });

  const { PDFName, PDFString } = await import('pdf-lib');

  const fileBytes = Buffer.from(ATTACHMENT_REPORT_TXT, 'utf8');

  // embedded file stream (FlateDecode, so extraction proves a lossless decode)
  const embeddedFile = doc.context.flateStream(fileBytes, {
    Type: 'EmbeddedFile',
    Subtype: 'text/plain',
    Params: { Size: fileBytes.length },
  });
  const embeddedFileRef = doc.context.register(embeddedFile);

  const fileSpec = doc.context.obj({
    Type: 'Filespec',
    F: PDFString.of('report.txt'),
    UF: PDFString.of('report.txt'),
    EF: doc.context.obj({ F: embeddedFileRef }),
  });
  const fileSpecRef = doc.context.register(fileSpec);

  // Text annotation first → the FileAttachment annotation lands at index 1
  const textAnnot = doc.context.obj({
    Type: 'Annot',
    Subtype: 'Text',
    Rect: [50, 600, 70, 620],
    Contents: PDFString.of('Just a sticky note'),
  });
  const textAnnotRef = doc.context.register(textAnnot);

  const fileAnnot = doc.context.obj({
    Type: 'Annot',
    Subtype: 'FileAttachment',
    Rect: [100, 600, 120, 620],
    Contents: PDFString.of('See attached report'),
    FS: fileSpecRef,
    Name: 'Paperclip',
  });
  const fileAnnotRef = doc.context.register(fileAnnot);

  page.node.set(PDFName.of('Annots'), doc.context.obj([textAnnotRef, fileAnnotRef]));
  return doc.save();
}

// --- PDF with form fields ---
async function createFormFieldsPdf() {
  const doc = await PDFDocument.create();
  const page = doc.addPage([612, 792]);
  const font = await doc.embedFont(StandardFonts.Helvetica);
  page.drawText('Form Fields Test', { x: 50, y: 750, size: 16, font });

  const form = doc.getForm();

  // text field with default value
  const textField = form.createTextField('fullName');
  textField.setText('John Doe');
  textField.addToPage(page, { x: 50, y: 680, width: 200, height: 24 });

  // empty text field with alternate name
  const emailField = form.createTextField('email');
  emailField.addToPage(page, { x: 50, y: 640, width: 200, height: 24 });

  // checkbox (checked)
  const checkbox = form.createCheckBox('agree');
  checkbox.addToPage(page, { x: 50, y: 600, width: 16, height: 16 });
  checkbox.check();

  // ensure checkbox /V and widget /AS are set (pdf-lib may not set /V properly)
  const { PDFName: FormPDFName } = await import('pdf-lib');
  checkbox.acroField.dict.set(FormPDFName.of('V'), FormPDFName.of('Yes'));
  const checkboxWidgets = checkbox.acroField.getWidgets();
  for (const w of checkboxWidgets) {
    w.dict.set(FormPDFName.of('AS'), FormPDFName.of('Yes'));
  }

  // checkbox (unchecked)
  const checkboxUnchecked = form.createCheckBox('newsletter');
  checkboxUnchecked.addToPage(page, { x: 50, y: 570, width: 16, height: 16 });

  // radio group
  const radioGroup = form.createRadioGroup('color');
  radioGroup.addOptionToPage('red', page, { x: 50, y: 530, width: 16, height: 16 });
  radioGroup.addOptionToPage('green', page, { x: 100, y: 530, width: 16, height: 16 });
  radioGroup.addOptionToPage('blue', page, { x: 150, y: 530, width: 16, height: 16 });
  radioGroup.select('green');

  // dropdown (combo box) with options
  const dropdown = form.createDropdown('country');
  dropdown.setOptions(['Germany', 'France', 'Italy', 'Spain']);
  dropdown.select('France');
  dropdown.addToPage(page, { x: 50, y: 480, width: 200, height: 24 });

  // list box with options
  const listBox = form.createOptionList('fruits');
  listBox.setOptions(['Apple', 'Banana', 'Cherry', 'Date']);
  listBox.select('Cherry');
  listBox.addToPage(page, { x: 50, y: 400, width: 200, height: 60 });

  return doc.save();
}

// Generate all fixtures
const fixtures = [
  ['metadata.pdf', createMetadataPdf],
  ['annotations.pdf', createAnnotationPdf],
  ['rich-annotations.pdf', createRichAnnotationPdf],
  ['links.pdf', createLinkPdf],
  ['bookmarks.pdf', createBookmarkPdf],
  ['minimal.pdf', createMinimalPdf],
  ['two-page.pdf', createTwoPagePdf],
  ['text.pdf', createTextPdf],
  ['rotated-page.pdf', createRotatedPagePdf],
  ['page-boxes.pdf', createPageBoxesPdf],
  ['tagged.pdf', createTaggedPdf],
  ['image.pdf', createImagePdf],
  ['rich-bookmarks.pdf', createRichBookmarkPdf],
  ['border-annotations.pdf', createBorderAnnotationPdf],
  ['form-fields.pdf', createFormFieldsPdf],
  ['einvoice-zugferd.pdf', createEInvoicePdf],
  ['file-attachment-annotation.pdf', createFileAttachmentAnnotationPdf],
  ['signed.pdf', createSignedPdf],
  ['positioned-text.pdf', createPositionedTextPdf],
];

for (const [name, generator] of fixtures) {
  const bytes = await generator();
  const path = resolve(fixturesDir, name);
  writeFileSync(path, bytes);
  console.log(`Created ${path} (${bytes.length} bytes)`);
}
