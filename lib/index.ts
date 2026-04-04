import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { withConcurrency } from './concurrency.js';
import { PDFiumDocument } from './document.js';
import { parseNativeError } from './errors.js';
import type {
  MergeDocumentInput,
  MergeDocumentsOptions,
  NativeAddon,
  SplitDocumentOptions,
} from './types.js';

const require = createRequire(import.meta.url);
const __dirname = dirname(fileURLToPath(import.meta.url));

type PdfInput = Buffer | string;

// ensure the dynamic linker can find libpdfium next to pdfium.node.
// note: LD_LIBRARY_PATH is read at process startup, so this only helps
// child processes or dlopen calls that haven't been resolved yet.
const addonDir = resolve(__dirname, '..', 'build', 'Release');
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
    return new PDFiumDocument(await withConcurrency(() => addon.loadDocument(input, password)));
  } catch (err) {
    throw parseNativeError(err);
  }
}

/**
 * Splits a PDF into multiple documents at the given page indices.
 * Each index marks the first page of a new chunk.
 */
export async function splitDocument(
  input: PdfInput,
  splitAt: number[],
  options?: SplitDocumentOptions,
): Promise<Buffer[] | void> {
  try {
    return await withConcurrency(() => addon.splitDocument(input, splitAt, options));
  } catch (err) {
    throw parseNativeError(err);
  }
}

/**
 * Combines multiple PDFs into a single document.
 */
export async function mergeDocuments(
  inputs: Array<PdfInput | MergeDocumentInput>,
  options?: MergeDocumentsOptions,
): Promise<Buffer | void> {
  try {
    return await withConcurrency(() => addon.mergeDocuments(inputs, options));
  } catch (err) {
    throw parseNativeError(err);
  }
}

export { concurrency } from './concurrency.js';
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
  AnnotationBorder,
  AnnotationType,
  Bookmark,
  DocumentMetadata,
  DocumentPermissions,
  FormField,
  FormFieldOption,
  FormFieldType,
  ImageColorspace,
  ImagePageObject,
  ImageRenderOptions,
  Link,
  LinkActionType,
  MergeDocumentInput,
  MergeDocumentsOptions,
  OtherPageObject,
  PageObject,
  PageObjectBounds,
  PageRenderOptions,
  QuadPoints,
  RGBA,
  SearchMatch,
  SearchOptions,
  SearchRect,
  SplitDocumentOptions,
  TextPageObject,
  TextRenderMode,
} from './types.js';
