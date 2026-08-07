import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import { addAttachments, loadDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

const tempFiles: string[] = [];
afterAll(async () => {
  const { rm } = await import('node:fs/promises');
  await Promise.all(tempFiles.map((f) => rm(f, { force: true })));
});

const XML = Buffer.from('<invoice><id>RE-1</id></invoice>', 'utf8');

describe('addAttachments', () => {
  it('embeds a file that reads back byte-for-byte', async () => {
    const pdf = await addAttachments(fixture('minimal.pdf'), [{ name: 'data.xml', data: XML }]);

    const doc = await loadDocument(pdf);
    expect(doc.metadata.attachmentCount).toBe(1);

    const [entry] = await doc.getAttachments();
    expect(entry.name).toBe('data.xml');

    const bytes = await doc.getAttachment(entry.index);
    expect(bytes.equals(XML)).toBe(true);

    doc.destroy();
  });

  it('embeds several files in one pass', async () => {
    const pdf = await addAttachments(fixture('minimal.pdf'), [
      { name: 'a.txt', data: Buffer.from('first') },
      { name: 'b.txt', data: Buffer.from('second') },
    ]);

    const doc = await loadDocument(pdf);
    const attachments = await doc.getAttachments();
    expect(attachments.map((a) => a.name).sort()).toEqual(['a.txt', 'b.txt']);

    const byName = async (name: string) => {
      const entry = attachments.find((a) => a.name === name)!;
      return (await doc.getAttachment(entry.index)).toString('utf8');
    };
    expect(await byName('a.txt')).toBe('first');
    expect(await byName('b.txt')).toBe('second');

    doc.destroy();
  });

  it('adds to a document that already has attachments', async () => {
    // einvoice-zugferd.pdf ships with two embedded files
    const pdf = await addAttachments(fixture('einvoice-zugferd.pdf'), [
      { name: 'extra.txt', data: Buffer.from('added') },
    ]);

    const doc = await loadDocument(pdf);
    expect(doc.metadata.attachmentCount).toBe(3);
    const names = (await doc.getAttachments()).map((a) => a.name);
    expect(names).toContain('factur-x.xml');
    expect(names).toContain('extra.txt');
    doc.destroy();
  });

  it('honours an explicit creation date instead of stamping now', async () => {
    const pdf = await addAttachments(fixture('minimal.pdf'), [
      {
        name: 'dated.txt',
        data: Buffer.from('x'),
        creationDate: 'D:20250101120000Z',
        modDate: 'D:20250202130000Z',
      },
    ]);

    const doc = await loadDocument(pdf);
    const [entry] = await doc.getAttachments();
    expect(entry.creationDate).toBe('D:20250101120000Z');
    expect(entry.modDate).toBe('D:20250202130000Z');
    doc.destroy();
  });

  it('stamps the current time when no creation date is given', async () => {
    const pdf = await addAttachments(fixture('minimal.pdf'), [
      { name: 'undated.txt', data: Buffer.from('x') },
    ]);

    const doc = await loadDocument(pdf);
    const [entry] = await doc.getAttachments();
    // PDFium writes "D:YYYYMMDDHHMMSS" without a timezone
    expect(entry.creationDate).toMatch(/^D:\d{14}$/);
    doc.destroy();
  });

  it('accepts a Buffer as input and leaves the source untouched', async () => {
    const source = readFileSync(fixture('minimal.pdf'));
    const before = Buffer.from(source);

    const pdf = await addAttachments(source, [{ name: 'x.txt', data: Buffer.from('x') }]);
    expect(source.equals(before)).toBe(true);

    const doc = await loadDocument(pdf);
    expect(doc.metadata.attachmentCount).toBe(1);
    doc.destroy();
  });

  it('writes to a file when output is given', async () => {
    const out = join(tmpdir(), `pdfium-addattach-${process.pid}.pdf`);
    tempFiles.push(out);

    const result = await addAttachments(fixture('minimal.pdf'), [{ name: 'data.xml', data: XML }], {
      output: out,
    });
    expect(result).toBeUndefined();

    const doc = await loadDocument(readFileSync(out));
    const [entry] = await doc.getAttachments();
    expect((await doc.getAttachment(entry.index)).equals(XML)).toBe(true);
    doc.destroy();
  });

  it('embeds an empty file, which PDFium then reads back as raw stream bytes', async () => {
    // The written PDF is correct: the stream is a valid FlateDecode of zero
    // bytes, and any conforming reader decodes it to nothing. PDFium's own
    // reader does not — CPDF_StreamAcc::ProcessFilteredData treats "decoded to
    // empty" as a decode failure and falls back to the raw compressed bytes
    // (the 8-byte zlib envelope 78 9c 03 00 00 00 00 01). Pinned here so the
    // quirk is visible rather than surprising; it applies to reading any PDF
    // with a zero-length embedded file, not just ones written here.
    const pdf = await addAttachments(fixture('minimal.pdf'), [
      { name: 'empty.txt', data: Buffer.alloc(0) },
    ]);

    const doc = await loadDocument(pdf);
    const [entry] = await doc.getAttachments();
    const bytes = await doc.getAttachment(entry.index);
    expect(bytes.toString('hex')).toBe('789c030000000001');
    doc.destroy();
  });

  it('rejects a duplicate name', async () => {
    // factur-x.xml already exists in this document
    await expect(
      addAttachments(fixture('einvoice-zugferd.pdf'), [{ name: 'factur-x.xml', data: XML }]),
    ).rejects.toThrow('duplicate name');
  });

  it('rejects an empty name', async () => {
    await expect(
      addAttachments(fixture('minimal.pdf'), [{ name: '', data: XML }]),
    ).rejects.toThrow();
  });

  it('rejects an empty attachment list', async () => {
    await expect(addAttachments(fixture('minimal.pdf'), [])).rejects.toThrow();
  });

  it('rejects an entry missing name or data', async () => {
    await expect(
      // @ts-expect-error deliberately malformed input
      addAttachments(fixture('minimal.pdf'), [{ data: XML }]),
    ).rejects.toThrow();
    await expect(
      // @ts-expect-error deliberately malformed input
      addAttachments(fixture('minimal.pdf'), [{ name: 'x.txt' }]),
    ).rejects.toThrow();
  });

  it('does not claim to produce a PDF/A-3 e-invoice', async () => {
    // Documented limitation, pinned here so it cannot regress silently:
    // PDFium cannot set /Subtype, so the MIME type is absent on write even
    // though it is reported on read when a PDF already carries one.
    const pdf = await addAttachments(fixture('minimal.pdf'), [{ name: 'factur-x.xml', data: XML }]);

    const doc = await loadDocument(pdf);
    const [entry] = await doc.getAttachments();
    expect(entry.mimeType).toBe('');
    doc.destroy();
  });
});
