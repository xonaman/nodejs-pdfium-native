import { createRequire } from 'node:module';
import { resolve } from 'node:path';
import { PDFiumDocument } from './document.js';
import { parseNativeError } from './errors.js';
import type { NativeAddon } from './types.js';

const require = createRequire(import.meta.url);

type PdfInput = Buffer | string;

// ensure the dynamic linker can find libpdfium next to pdfium.node
const addonDir = resolve(import.meta.dirname!, '..', 'build', 'Release');
if (process.platform === 'win32') {
  process.env.PATH = `${addonDir};${process.env.PATH ?? ''}`;
} else {
  process.env.LD_LIBRARY_PATH = `${addonDir}:${process.env.LD_LIBRARY_PATH ?? ''}`;
}

const addon: NativeAddon = require('../build/Release/pdfium.node');

/**
 * Opens a PDF document from a Buffer or file path.
 */
export async function loadDocument(input: PdfInput, password?: string): Promise<PDFiumDocument> {
  try {
    return new PDFiumDocument(await addon.loadDocument(input, password));
  } catch (err) {
    throw parseNativeError(err);
  }
}

export { PDFiumDocument } from './document.js';
export {
  PDFiumError,
  PDFiumFileError,
  PDFiumFormatError,
  PDFiumPasswordError,
  PDFiumSecurityError,
} from './errors.js';
export { PDFiumPage } from './page.js';
export type {
  Annotation,
  AnnotationType,
  Bookmark,
  DocumentMetadata,
  ImagePageObject,
  Link,
  OtherPageObject,
  PageObject,
  PageObjectBounds,
  PageObjectType,
  PageRenderOptions,
  PageSize,
  RGBA,
  SearchMatch,
  SearchOptions,
  SearchRect,
  TextPageObject,
} from './types.js';
