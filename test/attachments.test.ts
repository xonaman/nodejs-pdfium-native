import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import { addAttachments, loadDocument } from '../lib/index.js';
import { allLoaded } from './helpers.js';

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
const findByName = <T extends { name: string }>(list: readonly (T | null)[], name: string): T => {
  const found = list.find((a) => a?.name === name);
  if (!found) throw new Error(`attachment ${name} not found`);
  return found;
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

    const attachments = allLoaded(await doc.getAttachments());
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

  it('reports the /AFRelationship of each embedded file', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    const attachments = await doc.getAttachments();

    // the structured invoice is the machine-readable alternative to the page
    // content, while the notes file merely supplements it
    expect(findByName(attachments, 'factur-x.xml').afRelationship).toBe('Alternative');
    expect(findByName(attachments, 'notes.txt').afRelationship).toBe('Supplement');

    doc.destroy();
  });

  it('omits afRelationship when the PDF has no /AFRelationship entry', async () => {
    // addAttachments cannot write /AFRelationship, so a round-trip through it
    // is the absent case; the key must be missing rather than an empty string
    const pdf = await addAttachments(fixture('minimal.pdf'), [
      { name: 'data.xml', data: Buffer.from('<x/>') },
    ]);
    const doc = await loadDocument(pdf);

    const [entry] = await doc.getAttachments();
    expect(entry).toBeTruthy();
    expect(entry!.afRelationship).toBeUndefined();
    expect('afRelationship' in entry!).toBe(false);

    doc.destroy();
  });
});

// Regression tests for issue #34. damaged-attachment.pdf carries two embedded
// files whose /EmbeddedFiles name tree points its first file-specification at
// an object that does not exist, so FPDFDoc_GetAttachmentCount counts two while
// FPDFDoc_GetAttachment resolves only one. The failed index used to be dropped,
// which made the array shorter than metadata.attachmentCount with nothing to
// distinguish damage from a document that has no attachments at all.
describe('PDFiumDocument.getAttachments with an unloadable embedded file', () => {
  it('reports a null entry instead of silently shortening the array', async () => {
    const doc = await loadDocument(fixture('damaged-attachment.pdf'));

    expect(doc.metadata.attachmentCount).toBe(2);
    const attachments = await doc.getAttachments();

    // the invariant the old behaviour broke
    expect(attachments).toHaveLength(doc.metadata.attachmentCount);
    expect(attachments[0]).toBeNull();

    doc.destroy();
  });

  it('keeps the surviving attachment at its true index', async () => {
    const doc = await loadDocument(fixture('damaged-attachment.pdf'));
    const attachments = await doc.getAttachments();

    // array position is the document index, so the survivor does not slide
    // down into the hole the failed entry left
    expect(attachments).toHaveLength(2);
    const notes = attachments[1];
    expect(notes).toBeTruthy();
    expect(notes!.index).toBe(1);
    expect(notes!.name).toBe('notes.txt');
    expect(notes!.mimeType).toBe('text/plain');

    // and it is still fully readable
    const bytes = await doc.getAttachment(notes!.index);
    expect(bytes.toString('utf8')).toContain('Human-readable notes.');

    doc.destroy();
  });

  it('lets a caller tell damage apart from a document with no attachments', async () => {
    const damaged = await loadDocument(fixture('damaged-attachment.pdf'));
    const empty = await loadDocument(fixture('minimal.pdf'));

    const damagedList = await damaged.getAttachments();
    const emptyList = await empty.getAttachments();

    // both used to be indistinguishable from `list.every(Boolean)`'s point of
    // view once the failed entry was dropped
    expect(damagedList.some((a) => a === null)).toBe(true);
    expect(emptyList.some((a) => a === null)).toBe(false);
    expect(emptyList).toHaveLength(0);

    damaged.destroy();
    empty.destroy();
  });

  it('rejects when reading the bytes of the unloadable index', async () => {
    const doc = await loadDocument(fixture('damaged-attachment.pdf'));
    // the null in the listing and the rejection here are the same failure
    await expect(doc.getAttachment(0)).rejects.toThrow('Failed to get attachment');
    doc.destroy();
  });

  it('leaves an intact document free of null entries', async () => {
    const doc = await loadDocument(fixture('einvoice-zugferd.pdf'));
    const attachments = await doc.getAttachments();

    expect(attachments).toHaveLength(doc.metadata.attachmentCount);
    expect(attachments.every((a) => a !== null)).toBe(true);

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
