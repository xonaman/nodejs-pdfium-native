import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { withConcurrency } from './concurrency.js';
import { PDFiumDocument } from './document.js';
import { parseNativeError } from './errors.js';
import { isNativeIndex } from './validate.js';
import type {
  AssemblePagesOptions,
  MergeDocumentInput,
  MergeDocumentsOptions,
  NUpPagesOptions,
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

// The native addon is loaded through createRequire, so its export is `any` at
// the module boundary; assert it to the typed surface once, here.
const addon = require('../build/Release/pdfium.node') as NativeAddon;

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
  options: SplitDocumentOptions & { outputs: string[] },
): Promise<void>;
export async function splitDocument(
  input: PdfInput,
  splitAt: number[],
  options?: SplitDocumentOptions,
): Promise<Buffer[]>;
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
  options: MergeDocumentsOptions & { output: string },
): Promise<void>;
export async function mergeDocuments(
  inputs: Array<PdfInput | MergeDocumentInput>,
  options?: MergeDocumentsOptions,
): Promise<Buffer>;
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

/**
 * Builds a new PDF from selected pages of `input`, in the order given.
 *
 * Unlike {@link splitDocument}, which only cuts a document into consecutive
 * runs, the index list here is taken literally: pages may be reordered, left
 * out, or repeated. `assemblePages(pdf, [3, 0, 0])` yields a three-page
 * document whose first page is the original page 3 and whose next two are both
 * the original page 0.
 */
export async function assemblePages(
  input: PdfInput,
  pages: number[],
  options: AssemblePagesOptions & { output: string },
): Promise<void>;
export async function assemblePages(
  input: PdfInput,
  pages: number[],
  options?: AssemblePagesOptions,
): Promise<Buffer>;
export async function assemblePages(
  input: PdfInput,
  pages: number[],
  options?: AssemblePagesOptions,
): Promise<Buffer | void> {
  // ToInt32 in the native layer would wrap a fractional or oversized index onto
  // a different, valid page and silently assemble the wrong document.
  for (const page of pages) {
    if (!isNativeIndex(page)) {
      throw new RangeError(`Page index must be a 32-bit integer, got ${page}`);
    }
  }
  try {
    return await withConcurrency(() => addon.assemblePages(input, pages, options));
  } catch (err) {
    throw parseNativeError(err);
  }
}

/**
 * Imposes `columns × rows` source pages onto each page of a new document —
 * the classic "n-up" layout for handouts and proof sheets.
 *
 * The output sheet defaults to the size of the first source page, so a 2×2
 * n-up of A4 pages lands on A4 with each source page scaled to a quarter.
 */
export async function nUpPages(
  input: PdfInput,
  options: NUpPagesOptions & { output: string },
): Promise<void>;
export async function nUpPages(input: PdfInput, options: NUpPagesOptions): Promise<Buffer>;
export async function nUpPages(input: PdfInput, options: NUpPagesOptions): Promise<Buffer | void> {
  // Validate here rather than natively so callers get a plain RangeError
  // instead of a PDFiumError wrapping one.
  for (const [name, value] of [
    ['columns', options.columns],
    ['rows', options.rows],
  ] as const) {
    if (!Number.isInteger(value) || value < 1) {
      throw new RangeError(`${name} must be a positive integer, got ${value}`);
    }
  }
  try {
    return await withConcurrency(() => addon.nUpPages(input, options));
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
  AssemblePagesOptions,
  Attachment,
  Bookmark,
  DocumentMetadata,
  DestinationView,
  DocumentPermissions,
  FormField,
  FormFieldOption,
  FormFieldType,
  GetAttachmentOptions,
  GetCharactersOptions,
  GetSignatureContentsOptions,
  ImageColorspace,
  ImagePageObject,
  ImageRenderOptions,
  JavaScriptAction,
  Link,
  LinkActionType,
  MergeDocumentInput,
  MergeDocumentsOptions,
  NUpPagesOptions,
  NamedDestination,
  OtherPageObject,
  PageObject,
  PageObjectBounds,
  PageRenderOptions,
  QuadPoints,
  RGBA,
  SearchMatch,
  SearchOptions,
  SearchRect,
  Signature,
  SplitDocumentOptions,
  TextCharacter,
  TextPageObject,
  TextRenderMode,
} from './types.js';
