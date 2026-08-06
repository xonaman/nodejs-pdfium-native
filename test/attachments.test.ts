import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

// Exact bytes embedded as factur-x.xml by scripts/generate-fixtures.mjs.
// FlateDecode is lossless, so extraction must round-trip these bytes exactly.
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

const tempFiles: string[] = [];
afterAll(async () => {
  const { rm } = await import('node:fs/promises');
  await Promise.all(tempFiles.map((f) => rm(f, { force: true })));
});

// factur-x.xml / notes.txt embedded by scripts/generate-fixtures.mjs
const findByName = (list: { name: string }[], name: string) => {
  const found = list.find((a) => a.name === name);
  if (!found) throw new Error(`attachment ${name} not found`);
  return found as never;
};

describe('PDFiumDocument.getAttachments', () => {
  it('returns an empty array for a PDF without attachments', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    expect(doc.metadata.attachmentCount).toBe(0);
    const attachments = await doc.getAttachments();
    expect(Array.isArray(attachments)).toBe(true);
    expect(attachments).toHaveLength(0);
    doc.destroy();
  });

  it('lists embedded files with name, mimeType and dates', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    expect(doc.metadata.attachmentCount).toBe(2);

    const attachments = await doc.getAttachments();
    expect(attachments).toHaveLength(2);

    // indices are stable and unique
    expect(new Set(attachments.map((a) => a.index))).toEqual(new Set([0, 1]));

    const xml = findByName(attachments, 'factur-x.xml');
    expect(xml.mimeType).toBe('text/xml');
    expect(xml.creationDate).toMatch(/^D:2025/);

    const notes = findByName(attachments, 'notes.txt');
    expect(notes.mimeType).toBe('text/plain');

    doc.destroy();
  });
});

describe('PDFiumDocument.getAttachment', () => {
  it('reads the embedded factur-x.xml bytes', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    const attachments = await doc.getAttachments();
    const xml = findByName(attachments, 'factur-x.xml');

    const bytes = await doc.getAttachment(xml.index);
    expect(Buffer.isBuffer(bytes)).toBe(true);
    // exact-byte recovery, not just a substring match
    expect(bytes.equals(Buffer.from(FACTUR_X_XML, 'utf8'))).toBe(true);

    doc.destroy();
  });

  it('reads a different attachment by its index', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    const attachments = await doc.getAttachments();
    const notes = findByName(attachments, 'notes.txt');

    const bytes = await doc.getAttachment(notes.index);
    expect(bytes.toString('utf8')).toContain('Human-readable notes.');

    doc.destroy();
  });

  it('writes the attachment to a file when output is given', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    const attachments = await doc.getAttachments();
    const xml = findByName(attachments, 'factur-x.xml');

    const out = join(tmpdir(), `pdfium-attachment-${process.pid}-${xml.index}.xml`);
    tempFiles.push(out);

    const result = await doc.getAttachment(xml.index, { output: out });
    expect(result).toBeUndefined();

    const onDisk = readFileSync(out);
    const inMemory = await doc.getAttachment(xml.index);
    expect(onDisk.equals(inMemory)).toBe(true);

    doc.destroy();
  });

  it('rejects for an out-of-range index', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    await expect(doc.getAttachment(2)).rejects.toThrow('out of range');
    await expect(doc.getAttachment(-1)).rejects.toThrow('out of range');
    doc.destroy();
  });

  it('rejects a non-integer index instead of silently truncating', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    // ToInt32 would map these onto attachment 0 and return the wrong file
    await expect(doc.getAttachment(Number.NaN)).rejects.toThrow(RangeError);
    await expect(doc.getAttachment(Number.POSITIVE_INFINITY)).rejects.toThrow(RangeError);
    await expect(doc.getAttachment(1.5)).rejects.toThrow(RangeError);
    doc.destroy();
  });

  it('rejects an out-of-32-bit index that ToInt32 would wrap onto a real attachment', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    // 2**32 + 1 passes Number.isInteger but ToInt32-wraps to 1 (notes.txt); it
    // must reject, not silently resolve the wrong embedded file.
    await expect(doc.getAttachment(2 ** 32 + 1)).rejects.toThrow(RangeError);
    doc.destroy();
  });

  it('rejects after the document is destroyed', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    doc.destroy();
    await expect(doc.getAttachments()).rejects.toThrow('Document is destroyed');
    await expect(doc.getAttachment(0)).rejects.toThrow('Document is destroyed');
  });
});
