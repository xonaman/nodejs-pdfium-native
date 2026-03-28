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
  /** Page rotation: 0 = none, 1 = 90° CW, 2 = 180°, 3 = 270° CW. */
  rotation?: 0 | 1 | 2 | 3;
  /** Render with transparent background instead of white (PNG only). */
  transparent?: boolean;
  /** Whether to render annotations (default: true). */
  renderAnnotations?: boolean;
  /** Render in grayscale. */
  grayscale?: boolean;
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
  /** Font weight (e.g. 400 = normal, 700 = bold). Absent if unavailable. */
  fontWeight?: number;
  /** Italic angle in degrees counterclockwise from vertical. Negative means right-sloping. */
  italicAngle?: number;
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
  /** Author of the annotation. */
  author: string;
  /** Subject of the annotation. */
  subject: string;
  /** Creation date as a PDF date string (e.g. "D:20250328..."). */
  creationDate: string;
  /** Modification date as a PDF date string. */
  modDate: string;
  /** Raw annotation flags bitmask (see PDF spec Table 165). */
  flags: number;
}

export type LinkActionType = 'goto' | 'remoteGoto' | 'uri' | 'launch' | 'embeddedGoto' | 'unknown';

export interface Link {
  bounds?: PageObjectBounds;
  url?: string;
  pageIndex?: number;
  /** The type of action this link performs. */
  actionType?: LinkActionType;
  /** Destination X coordinate (for internal links). */
  destX?: number;
  /** Destination Y coordinate (for internal links). */
  destY?: number;
  /** Destination zoom level (for internal links). */
  destZoom?: number;
}

export interface Bookmark {
  title: string;
  pageIndex?: number;
  children?: Bookmark[];
}

export interface DocumentPermissions {
  print: boolean;
  modify: boolean;
  copy: boolean;
  annotate: boolean;
  fillForms: boolean;
  extractForAccessibility: boolean;
  assemble: boolean;
  printHighQuality: boolean;
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
  /** Document permission flags. All true if unprotected. */
  permissions: DocumentPermissions;
}

// native addon bindings (internal)
export interface NativePage {
  readonly number: number;
  readonly width: number;
  readonly height: number;
  readonly objectCount: number;
  getText(): Promise<string>;
  render(options?: PageRenderOptions): Promise<Buffer | void>;
  getObject(index: number): Promise<PageObject>;
  getLinks(): Promise<Link[]>;
  search(text: string, options?: SearchOptions): Promise<SearchMatch[]>;
  getAnnotations(): Promise<Annotation[]>;
  close(): void;
}

export interface NativeDocument {
  readonly pageCount: number;
  readonly metadata: DocumentMetadata;
  getPage(index: number): Promise<NativePage>;
  getBookmarks(): Promise<Bookmark[]>;
  destroy(): void;
}

export interface NativeAddon {
  loadDocument(input: Buffer | string, password?: string): Promise<NativeDocument>;
}
