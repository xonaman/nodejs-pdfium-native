import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

// Exact placeholder blobs stamped as /Contents by scripts/generate-fixtures.mjs.
// They are not real PKCS#7 structures — PDFium performs no verification and
// returns the bytes verbatim, so extraction must round-trip them exactly.
const SIG_CONTENTS = Buffer.from('308006092a864886f70d010702a0803080020101', 'hex');
const CERT_SIG_CONTENTS = Buffer.from('3082010a0282010100c0ffee', 'hex');

const tempFiles: string[] = [];
afterAll(async () => {
  const { rm } = await import('node:fs/promises');
  await Promise.all(tempFiles.map((f) => rm(f, { force: true })));
});

describe('PDFiumDocument.getSignatures', () => {
  it('returns an empty array for a PDF without an AcroForm', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    expect(doc.metadata.signatureCount).toBe(0);
    const signatures = await doc.getSignatures();
    expect(Array.isArray(signatures)).toBe(true);
    expect(signatures).toHaveLength(0);
    doc.destroy();
  });

  it('returns an empty array for a form with no signature fields', async () => {
    // form-fields.pdf has an AcroForm, but every field is a text/checkbox/etc.
    const doc = await loadDocument(fixture('form-fields.pdf'));
    expect(doc.metadata.signatureCount).toBe(0);
    expect(await doc.getSignatures()).toHaveLength(0);
    doc.destroy();
  });

  it('lists both signature fields with encoding, reason and time', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    expect(doc.metadata.signatureCount).toBe(2);

    const signatures = await doc.getSignatures();
    expect(signatures).toHaveLength(2);
    expect(signatures.map((s) => s.index)).toEqual([0, 1]);

    const [approval, certification] = signatures;

    expect(approval.subFilter).toBe('adbe.pkcs7.detached');
    expect(approval.reason).toBe('I approve this document');
    expect(approval.time).toBe("D:20250101120000+01'00'");

    expect(certification.subFilter).toBe('ETSI.CAdES.detached');
    expect(certification.time).toBe('D:20250202093000Z');
    // no /Reason in the certification signature dictionary
    expect(certification.reason).toBeUndefined();

    doc.destroy();
  });

  it('reports docMdpPermission only for the certification signature', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    const [approval, certification] = await doc.getSignatures();

    // an ordinary approval signature has no /Reference -> /DocMDP entry
    expect(approval.docMdpPermission).toBeUndefined();
    expect(certification.docMdpPermission).toBe(2);

    doc.destroy();
  });

  it('returns the byte range as the flat (offset, length) pairs from the PDF', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    const [approval, certification] = await doc.getSignatures();

    expect(approval.byteRange).toEqual([0, 840, 1560, 1234]);
    expect(certification.byteRange).toEqual([0, 200, 900, 300]);

    doc.destroy();
  });

  it('reports the /Contents length without reading the blob', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    const [approval, certification] = await doc.getSignatures();

    expect(approval.contentsLength).toBe(SIG_CONTENTS.length);
    expect(certification.contentsLength).toBe(CERT_SIG_CONTENTS.length);

    doc.destroy();
  });

  it('rejects after the document is destroyed', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    doc.destroy();
    await expect(doc.getSignatures()).rejects.toThrow('Document is destroyed');
  });
});

describe('PDFiumDocument.getSignatureContents', () => {
  it('reads the raw signature bytes verbatim', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));

    const bytes = await doc.getSignatureContents(0);
    expect(Buffer.isBuffer(bytes)).toBe(true);
    expect(bytes.equals(SIG_CONTENTS)).toBe(true);

    doc.destroy();
  });

  it('reads a different signature by its index', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));

    const bytes = await doc.getSignatureContents(1);
    expect(bytes.equals(CERT_SIG_CONTENTS)).toBe(true);

    doc.destroy();
  });

  it('writes the signature to a file when output is given', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));

    const out = join(tmpdir(), `pdfium-signature-${process.pid}.der`);
    tempFiles.push(out);

    const result = await doc.getSignatureContents(0, { output: out });
    expect(result).toBeUndefined();
    expect(readFileSync(out).equals(SIG_CONTENTS)).toBe(true);

    doc.destroy();
  });

  it('rejects for an out-of-range index', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    await expect(doc.getSignatureContents(2)).rejects.toThrow('out of range');
    await expect(doc.getSignatureContents(-1)).rejects.toThrow('out of range');
    doc.destroy();
  });

  it('rejects a non-integer index instead of silently truncating', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    // ToInt32 would map these onto signature 0 and return the wrong blob
    await expect(doc.getSignatureContents(Number.NaN)).rejects.toThrow(RangeError);
    await expect(doc.getSignatureContents(Number.POSITIVE_INFINITY)).rejects.toThrow(RangeError);
    await expect(doc.getSignatureContents(1.5)).rejects.toThrow(RangeError);
    doc.destroy();
  });

  it('rejects an out-of-32-bit index that ToInt32 would wrap onto a real signature', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    // 2**32 + 1 passes Number.isInteger but ToInt32-wraps to 1 (the cert sig)
    await expect(doc.getSignatureContents(2 ** 32 + 1)).rejects.toThrow(RangeError);
    doc.destroy();
  });

  it('rejects after the document is destroyed', async () => {
    const doc = await loadDocument(fixture('signed.pdf'));
    doc.destroy();
    await expect(doc.getSignatureContents(0)).rejects.toThrow('Document is destroyed');
  });
});
