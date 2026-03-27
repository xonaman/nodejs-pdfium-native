import { unlinkSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  loadDocument,
  PDFiumDocument,
  PDFiumError,
  PDFiumFileError,
  PDFiumFormatError,
} from '../index.js';
import { pdfBuffer } from './fixtures.js';

const tmpFilePath = resolve(import.meta.dirname!, 'test-fixture.pdf');

beforeAll(() => {
  writeFileSync(tmpFilePath, pdfBuffer);
});

afterAll(() => {
  try {
    unlinkSync(tmpFilePath);
  } catch {
    // ignore
  }
});

describe('loadDocument', () => {
  it('loads a PDF from a Buffer', async () => {
    const doc = await loadDocument(pdfBuffer);
    expect(doc).toBeInstanceOf(PDFiumDocument);
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });

  it('loads a PDF from a file path', async () => {
    const doc = await loadDocument(tmpFilePath);
    expect(doc).toBeInstanceOf(PDFiumDocument);
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });

  it('rejects with PDFiumFormatError for invalid data', async () => {
    await expect(loadDocument(Buffer.from('not a pdf'))).rejects.toThrow(PDFiumFormatError);
    try {
      await loadDocument(Buffer.from('not a pdf'));
    } catch (err) {
      expect(err).toBeInstanceOf(PDFiumError);
      expect((err as PDFiumError).code).toBe('FORMAT');
    }
  });

  it('rejects with PDFiumFileError for non-existent file', async () => {
    await expect(loadDocument('/tmp/does-not-exist.pdf')).rejects.toThrow(PDFiumFileError);
    try {
      await loadDocument('/tmp/does-not-exist.pdf');
    } catch (err) {
      expect(err).toBeInstanceOf(PDFiumError);
      expect((err as PDFiumError).code).toBe('FILE');
    }
  });

  it('rejects with an error for wrong password', async () => {
    // since our test PDF is not encrypted, this just opens normally
    const doc = await loadDocument(pdfBuffer, 'unused-password');
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });
});
