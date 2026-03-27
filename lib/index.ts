import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);

type PdfInput = Buffer | string;

export interface PageSize {
  width: number;
  height: number;
}

export interface PageRenderOptions {
  /** Scale factor (default: 1, meaning 72 DPI). Use 3–4 for print quality. */
  scale?: number;
  /** Override render width in pixels. */
  width?: number;
  /** Override render height in pixels. */
  height?: number;
  /** Output format (default: 'jpeg'). */
  format?: 'jpeg' | 'png';
  /** JPEG quality 1–100 (default: 100). Ignored for PNG. */
  quality?: number;
  /** Write to this file path instead of returning a Buffer. */
  output?: string;
}

export interface PageObjectBounds {
  left: number;
  bottom: number;
  right: number;
  top: number;
}

export interface RGBA {
  r: number;
  g: number;
  b: number;
  a: number;
}

interface BasePageObject {
  bounds: PageObjectBounds;
  fillColor: RGBA | null;
  strokeColor: RGBA | null;
}

export interface TextPageObject extends BasePageObject {
  type: 'text';
  text: string;
  fontSize: number;
  fontName: string;
}

export interface ImagePageObject extends BasePageObject {
  type: 'image';
  imageWidth: number;
  imageHeight: number;
}

export interface OtherPageObject extends BasePageObject {
  type: 'path' | 'shading' | 'form' | 'unknown';
}

export type PageObject = TextPageObject | ImagePageObject | OtherPageObject;
export type PageObjectType = PageObject['type'];

export interface SearchOptions {
  caseSensitive?: boolean;
  wholeWord?: boolean;
}

export interface SearchMatch {
  charIndex: number;
  length: number;
  rects: SearchRect[];
}

export interface SearchRect {
  left: number;
  top: number;
  right: number;
  bottom: number;
}

export type AnnotationType =
  | 'text'
  | 'link'
  | 'freetext'
  | 'line'
  | 'square'
  | 'circle'
  | 'highlight'
  | 'underline'
  | 'squiggly'
  | 'strikeout'
  | 'stamp'
  | 'ink'
  | 'popup'
  | 'widget'
  | 'redact'
  | 'unknown';

export interface Annotation {
  type: AnnotationType;
  bounds?: PageObjectBounds;
  contents: string;
  color: RGBA | null;
}

export interface Link {
  bounds?: PageObjectBounds;
  url?: string;
  pageIndex?: number;
}

export interface Bookmark {
  title: string;
  pageIndex?: number;
  children?: Bookmark[];
}

export interface DocumentMetadata {
  title: string;
  author: string;
  subject: string;
  keywords: string;
  creator: string;
  producer: string;
  creationDate: string;
  modDate: string;
  pdfVersion: number;
}

// native addon bindings
interface NativePage {
  readonly number: number;
  readonly width: number;
  readonly height: number;
  readonly size: PageSize;
  readonly objectCount: number;
  getText(): string;
  render(options?: PageRenderOptions): Promise<Buffer | void>;
  getObject(index: number): PageObject;
  getLinks(): Link[];
  search(text: string, options?: SearchOptions): SearchMatch[];
  getAnnotations(): Annotation[];
  close(): void;
}

interface NativeDocument {
  readonly pageCount: number;
  readonly metadata: DocumentMetadata;
  getPage(index: number): Promise<NativePage>;
  getBookmarks(): Bookmark[];
  destroy(): void;
}

interface NativeAddon {
  loadDocument(input: PdfInput, password?: string): Promise<NativeDocument>;
}

const addon: NativeAddon = require('../build/Release/pdfium.node');

/**
 * A page in a PDF document.
 */
export class PDFiumPage {
  /** Page width in points (1 pt = 1/72 inch). */
  readonly width: number;
  /** Page height in points (1 pt = 1/72 inch). */
  readonly height: number;
  /** Page dimensions in points. */
  readonly size: PageSize;
  /** 0-based page index. */
  readonly number: number;
  /** Number of page objects (text spans, paths, images, etc.). */
  readonly objectCount: number;

  /** @internal */
  constructor(private native: NativePage) {
    this.width = native.width;
    this.height = native.height;
    this.size = native.size;
    this.number = native.number;
    this.objectCount = native.objectCount;
  }

  /** Extracts all text from the page. */
  getText(): string {
    return this.native.getText();
  }

  /** Renders the page to a file. */
  render(options: PageRenderOptions & { output: string }): Promise<void>;
  /** Renders the page to an encoded image buffer (JPEG or PNG). */
  render(options?: PageRenderOptions): Promise<Buffer>;
  render(options?: PageRenderOptions): Promise<Buffer | void> {
    return this.native.render(options);
  }

  /** Returns the page object at the given index with type and bounds. */
  getObject(index: number): PageObject {
    return this.native.getObject(index);
  }

  /** Closes the page and frees resources. */
  close(): void {
    this.native.close();
  }

  /** Returns all links on the page. */
  getLinks(): Link[] {
    return this.native.getLinks();
  }

  /** Searches for text on the page. Returns matches with positions and bounding rects. */
  search(text: string, options?: SearchOptions): SearchMatch[] {
    return this.native.search(text, options);
  }

  /** Returns all annotations on the page. */
  getAnnotations(): Annotation[] {
    return this.native.getAnnotations();
  }
}

/**
 * A loaded PDF document.
 */
export class PDFiumDocument {
  /** Total number of pages. */
  readonly pageCount: number;
  /** Document metadata (title, author, dates, etc.). */
  readonly metadata: DocumentMetadata;

  /** @internal */
  constructor(private native: NativeDocument) {
    this.pageCount = native.pageCount;
    this.metadata = native.metadata;
  }

  /** Gets a page by 0-based index. */
  async getPage(index: number): Promise<PDFiumPage> {
    return new PDFiumPage(await this.native.getPage(index));
  }

  /** Iterates over all pages. Caller is responsible for closing each page. */
  async *pages(): AsyncGenerator<PDFiumPage> {
    for (let i = 0; i < this.pageCount; i++) {
      yield this.getPage(i);
    }
  }

  /** Closes the document and frees all resources. */
  destroy(): void {
    this.native.destroy();
  }

  /** Returns the bookmark/outline tree. */
  getBookmarks(): Bookmark[] {
    return this.native.getBookmarks();
  }
}

/**
 * Opens a PDF document from a Buffer or file path.
 */
export async function loadDocument(input: PdfInput, password?: string): Promise<PDFiumDocument> {
  return new PDFiumDocument(await addon.loadDocument(input, password));
}
