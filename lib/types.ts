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
  /** Enable LCD-optimized text rendering (sub-pixel anti-aliasing). */
  lcdText?: boolean;
}

export interface PageObjectBounds {
  /** Left edge in page points. */
  left: number;
  /** Bottom edge in page points. */
  bottom: number;
  /** Right edge in page points. */
  right: number;
  /** Top edge in page points. */
  top: number;
}

export interface RGBA {
  /** Red channel (0–255). */
  r: number;
  /** Green channel (0–255). */
  g: number;
  /** Blue channel (0–255). */
  b: number;
  /** Alpha channel (0–255, where 255 is fully opaque). */
  a: number;
}

interface BasePageObject {
  /** Bounding box in page coordinates (points). */
  bounds: PageObjectBounds;
  /** Fill color, or null if none. */
  fillColor: RGBA | null;
  /** Stroke color, or null if none. */
  strokeColor: RGBA | null;
}

export type TextRenderMode =
  | 'fill'
  | 'stroke'
  | 'fillStroke'
  | 'invisible'
  | 'fillClip'
  | 'strokeClip'
  | 'fillStrokeClip'
  | 'clip'
  | 'unknown';

export interface TextPageObject extends BasePageObject {
  type: 'text';
  /** The text content of this object. */
  text: string;
  /** Font size in points. */
  fontSize: number;
  /** PostScript name of the font. */
  fontName: string;
  /** Font weight (e.g. 400 = normal, 700 = bold). Absent if unavailable. */
  fontWeight?: number;
  /** Italic angle in degrees counterclockwise from vertical. Negative means right-sloping. */
  italicAngle?: number;
  /** Text render mode (e.g. 'fill', 'stroke', 'fillStroke'). */
  renderMode?: TextRenderMode;
  /** Font family name. */
  fontFamily?: string;
  /** Whether the font is embedded in the PDF. */
  isEmbedded?: boolean;
  /** Raw PDF font descriptor flags bitmask (see PDF spec Table 123). */
  fontFlags?: number;
}

export type ImageColorspace =
  | 'deviceGray'
  | 'deviceRGB'
  | 'deviceCMYK'
  | 'calGray'
  | 'calRGB'
  | 'lab'
  | 'iccBased'
  | 'separation'
  | 'deviceN'
  | 'indexed'
  | 'pattern'
  | 'unknown';

export interface ImagePageObject extends BasePageObject {
  type: 'image';
  /** Intrinsic image width in pixels. */
  imageWidth: number;
  /** Intrinsic image height in pixels. */
  imageHeight: number;
  /** Horizontal DPI of the image. */
  horizontalDpi?: number;
  /** Vertical DPI of the image. */
  verticalDpi?: number;
  /** Number of bits per pixel. */
  bitsPerPixel?: number;
  /** Image color space (e.g. 'deviceRGB', 'deviceCMYK'). */
  colorspace?: ImageColorspace;
  /** Compression filters applied to the image (e.g. 'FlateDecode', 'DCTDecode'). */
  filters?: string[];
}

export interface OtherPageObject extends BasePageObject {
  type: 'path' | 'shading' | 'form' | 'unknown';
}

export type PageObject = TextPageObject | ImagePageObject | OtherPageObject;

export interface SearchOptions {
  /** Match case when searching (default: false). */
  caseSensitive?: boolean;
  /** Only match whole words (default: false). */
  wholeWord?: boolean;
  /** Allow overlapping matches (default: false). */
  consecutive?: boolean;
}

export interface SearchMatch {
  /** 0-based character index where the match starts. */
  charIndex: number;
  /** Number of characters in the match. */
  length: number;
  /** The actual matched text from the page. */
  matchedText?: string;
  /** Bounding rectangles covering the matched text. */
  rects: SearchRect[];
}

export interface SearchRect {
  /** Left edge in page points. */
  left: number;
  /** Top edge in page points. */
  top: number;
  /** Right edge in page points. */
  right: number;
  /** Bottom edge in page points. */
  bottom: number;
}

export type AnnotationType =
  | 'text'
  | 'link'
  | 'freetext'
  | 'line'
  | 'square'
  | 'circle'
  | 'polygon'
  | 'polyline'
  | 'highlight'
  | 'underline'
  | 'squiggly'
  | 'strikeout'
  | 'stamp'
  | 'caret'
  | 'ink'
  | 'popup'
  | 'fileattachment'
  | 'sound'
  | 'widget'
  | 'redact'
  | 'watermark'
  | 'unknown';

export interface AnnotationBorder {
  /** Horizontal corner radius. */
  horizontalRadius: number;
  /** Vertical corner radius. */
  verticalRadius: number;
  /** Border width in points. */
  width: number;
}

export interface QuadPoints {
  /** Top-left X. */
  x1: number;
  /** Top-left Y. */
  y1: number;
  /** Top-right X. */
  x2: number;
  /** Top-right Y. */
  y2: number;
  /** Bottom-left X. */
  x3: number;
  /** Bottom-left Y. */
  y3: number;
  /** Bottom-right X. */
  x4: number;
  /** Bottom-right Y. */
  y4: number;
}

export interface Annotation {
  /** Annotation subtype (e.g. 'highlight', 'text', 'link'). */
  type: AnnotationType;
  /** Bounding box in page coordinates, if available. */
  bounds?: PageObjectBounds;
  /** Text contents of the annotation. */
  contents: string;
  /** Annotation color, or null if none. */
  color: RGBA | null;
  /** Interior (fill) color for markup annotations. */
  interiorColor?: RGBA;
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
  /** Border style of the annotation. */
  border?: AnnotationBorder;
  /** Quad points for text markup annotations (highlight, underline, etc.). */
  quadPoints?: QuadPoints[];
}

export type LinkActionType = 'goto' | 'remoteGoto' | 'uri' | 'launch' | 'embeddedGoto' | 'unknown';

export interface Link {
  /** Bounding box in page coordinates, if available. */
  bounds?: PageObjectBounds;
  /** External URL target, if this is a URI link. */
  url?: string;
  /** Target page index for internal (goto) links. */
  pageIndex?: number;
  /** The type of action this link performs. */
  actionType?: LinkActionType;
  /** Destination X coordinate (for internal links). */
  destX?: number;
  /** Destination Y coordinate (for internal links). */
  destY?: number;
  /** Destination zoom level (for internal links). */
  destZoom?: number;
  /** File path for remote goto or launch actions. */
  filePath?: string;
}

export interface Bookmark {
  /** Display title of the bookmark. */
  title: string;
  /** Target page index, if the bookmark points to a page. */
  pageIndex?: number;
  /** Whether this bookmark node is initially open (expanded) in the viewer. */
  open: boolean;
  /** The type of action this bookmark performs. */
  actionType?: LinkActionType;
  /** External URL target for URI bookmarks. */
  url?: string;
  /** Destination X coordinate. */
  destX?: number;
  /** Destination Y coordinate. */
  destY?: number;
  /** Destination zoom level. */
  destZoom?: number;
  /** Child bookmarks forming a tree. */
  children?: Bookmark[];
}

export interface DocumentPermissions {
  /** Printing the document. */
  print: boolean;
  /** Modifying document contents. */
  modify: boolean;
  /** Copying or extracting text and graphics. */
  copy: boolean;
  /** Adding or modifying annotations. */
  annotate: boolean;
  /** Filling in form fields. */
  fillForms: boolean;
  /** Extracting text and graphics for accessibility. */
  extractForAccessibility: boolean;
  /** Assembling the document (insert, rotate, delete pages). */
  assemble: boolean;
  /** High-resolution printing. */
  printHighQuality: boolean;
}

export interface DocumentMetadata {
  /** Document title. */
  title: string;
  /** Document author. */
  author: string;
  /** Document subject. */
  subject: string;
  /** Document keywords. */
  keywords: string;
  /** Application that created the original document. */
  creator: string;
  /** Application that produced the PDF. */
  producer: string;
  /** Creation date as a PDF date string. */
  creationDate: string;
  /** Last modification date as a PDF date string. */
  modDate: string;
  /** PDF version as an integer (e.g. 17 for PDF 1.7). */
  pdfVersion: number;
  /** Document permission flags. All true if unprotected. */
  permissions: DocumentPermissions;
  /** Whether the PDF is a tagged PDF (structured content). */
  isTagged: boolean;
  /** Document language (e.g. 'en-US'). Empty string if not set. */
  language: string;
  /** Number of digital signatures in the document. */
  signatureCount: number;
  /** Number of file attachments in the document. */
  attachmentCount: number;
  /** Permanent file identifier (hex string). */
  permanentId?: string;
  /** Changing file identifier (hex string, updated on each save). */
  changingId?: string;
}

// native addon bindings (internal)
export interface NativePage {
  readonly number: number;
  readonly width: number;
  readonly height: number;
  readonly objectCount: number;
  readonly rotation: number;
  readonly hasTransparency: boolean;
  readonly label?: string;
  readonly cropBox?: PageObjectBounds;
  readonly trimBox?: PageObjectBounds;
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
