export interface PageRenderOptions {
  /** Scale factor (default: 1, meaning 72 DPI). Use 3–4 for print quality. */
  scale?: number;
  /** Override render width in pixels. */
  width?: number;
  /** Override render height in pixels. */
  height?: number;
  /** Output format (default: 'png'). */
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

export interface ImageRenderOptions {
  /** Output format (default: 'png'). Use 'raw' for original encoded stream bytes. */
  format?: 'jpeg' | 'png' | 'raw';
  /** JPEG quality 1–100 (default: 100). Ignored for PNG and raw. */
  quality?: number;
  /** Write to this file path instead of returning a Buffer. */
  output?: string;
  /** Use the rendered bitmap (applies image mask and transformation matrix). Default: false. Ignored when format is 'raw'. */
  rendered?: boolean;
}

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
  /** Renders the image to a file. */
  render(options: ImageRenderOptions & { output: string }): Promise<void>;
  /** Renders the image to an encoded buffer (JPEG, PNG, or raw stream bytes). */
  render(options?: ImageRenderOptions): Promise<Buffer>;
}

export interface OtherPageObject extends BasePageObject {
  type: 'path' | 'shading' | 'form' | 'unknown';
}

export type PageObject = TextPageObject | ImagePageObject | OtherPageObject;

/**
 * A single character on a page, with the geometry `getText()` discards.
 */
export interface TextCharacter {
  /** 0-based index of this character in the page's text. */
  index: number;
  /**
   * The character itself. Empty string when the glyph has no Unicode mapping —
   * check `hasUnicodeMapError` before trusting it.
   */
  char: string;
  /** Unicode code point, or 0 if the glyph has no mapping. */
  unicode: number;
  /**
   * Tight bounding box in page points, hugging the glyph's ink. Absent if
   * PDFium cannot determine it.
   */
  bounds?: PageObjectBounds;
  /** X coordinate of the baseline origin in page points. Absent if unavailable. */
  x?: number;
  /** Y coordinate of the baseline origin in page points. Absent if unavailable. */
  y?: number;
  /** Font size in points (the typographic "em" size). */
  fontSize: number;
  /** PostScript name of the font (e.g. 'Helvetica-Bold'). Empty string if unavailable. */
  fontName: string;
  /** Raw PDF font descriptor flags bitmask (see PDF spec Table 123). */
  fontFlags: number;
  /** Font weight (e.g. 400 = normal, 700 = bold). Absent if unavailable. */
  fontWeight?: number;
  /**
   * Rotation of the character in radians, normalized to `[0, 2π)`. 0 for
   * upright text.
   *
   * Note the direction: PDFium derives this as `atan2(c, a)` from the text
   * matrix, so it runs *clockwise*. Text rotated 45° counterclockwise on the
   * page reports ≈ 5.4978 (2π − π/4), not ≈ 0.7854.
   */
  angle: number;
  /**
   * Whether PDFium synthesized this character rather than reading it from the
   * content stream — line breaks between text runs, and spaces inferred from
   * glyph spacing. Synthesized characters carry no real font, so `fontName` is
   * empty, `fontSize` is 1 and `fontWeight` is absent. Useful when mapping text
   * offsets back to the original PDF.
   */
  isGenerated: boolean;
  /** Whether this character is a hyphen (relevant when de-hyphenating lines). */
  isHyphen: boolean;
  /** Whether the glyph has no reliable Unicode mapping, making `char` unreliable. */
  hasUnicodeMapError: boolean;
}

/**
 * A node in a tagged PDF's structure tree — the logical document outline
 * (headings, paragraphs, tables, figures) behind the visual layout, which is
 * what screen readers follow.
 */
export interface StructElement {
  /** Structure type (/S), e.g. 'H1', 'P', 'Table', 'Figure'. */
  type: string;
  /** Object type (/Type), normally 'StructElem'. Absent if not set. */
  objType?: string;
  /** Human-readable title (/T). Absent if not set. */
  title?: string;
  /** Alternate text (/Alt) — the accessibility description. Absent if not set. */
  altText?: string;
  /** Replacement text (/ActualText) for content whose glyphs differ from its meaning. */
  actualText?: string;
  /** Element identifier (/ID). Absent if not set. */
  id?: string;
  /** Language override (/Lang) for this subtree, e.g. 'de-DE'. Absent if not set. */
  lang?: string;
  /** Marked-content ID tying this element to page content. Absent if the element has none. */
  markedContentId?: number;
  /** Child elements. Absent when the element has no element children. */
  children?: StructElement[];
}

export interface GetCharactersOptions {
  /** First character index to return (default: 0). */
  start?: number;
  /** How many characters to return (default: all remaining). */
  count?: number;
}

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
  /**
   * 0-based index of this annotation on the page. Pass it to
   * {@link NativePage.getAnnotationAttachment} to read a file-attachment
   * annotation's embedded bytes.
   */
  index: number;
  /** Annotation subtype (e.g. 'highlight', 'text', 'link'). */
  type: AnnotationType;
  /**
   * For `'fileattachment'` annotations: the embedded file's name
   * (e.g. 'report.pdf'). Absent for other annotation types.
   */
  fileName?: string;
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

export type FormFieldType =
  | 'pushButton'
  | 'checkbox'
  | 'radioButton'
  | 'comboBox'
  | 'listBox'
  | 'textField'
  | 'signature'
  | 'unknown';

export interface FormFieldOption {
  /** Display label for this option. */
  label: string;
  /** Whether this option is currently selected. */
  isSelected: boolean;
}

export interface FormField {
  /** Form field type. */
  type: FormFieldType;
  /** Fully qualified field name. */
  name: string;
  /** Current field value. */
  value: string;
  /** Alternate (tooltip) name, if set. */
  alternateName?: string;
  /** Export value for checkboxes and radio buttons. */
  exportValue?: string;
  /** Raw form field flags bitmask. */
  flags: number;
  /** Bounding box in page coordinates, if available. */
  bounds?: PageObjectBounds;
  /** Whether the field is checked (checkbox/radio only). */
  isChecked: boolean;
  /** Available options for combo box and list box fields. */
  options?: FormFieldOption[];
}

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

export interface Attachment {
  /** 0-based index of the attachment in the document's embedded-files name tree. */
  index: number;
  /** File name as stored in the /EmbeddedFiles name tree (e.g. 'factur-x.xml'). */
  name: string;
  /** MIME type (/Subtype), e.g. 'text/xml'. Empty string if the PDF omits it. */
  mimeType: string;
  /** Creation date as a PDF date string (e.g. 'D:20250101120000Z'). Absent if not set. */
  creationDate?: string;
  /** Modification date as a PDF date string. Absent if not set. */
  modDate?: string;
}

export interface GetAttachmentOptions {
  /** Write the attachment bytes to this file path instead of returning a Buffer. */
  output?: string;
}

/**
 * A digital signature as declared by the PDF.
 *
 * These values come straight from the signature dictionary — nothing here is
 * cryptographically verified. A present, well-formed signature says only that
 * the document *claims* to be signed.
 */
export interface Signature {
  /** 0-based index of the signature in the document's AcroForm field list. */
  index: number;
  /**
   * Encoding of the signature value (/SubFilter), e.g. 'adbe.pkcs7.detached'
   * or 'ETSI.CAdES.detached'. Empty string if the PDF omits it.
   */
  subFilter: string;
  /** Reason given for signing (/Reason). Absent if not set. */
  reason?: string;
  /**
   * Time of signing (/M) as a PDF date string (e.g. "D:20250101120000+01'00'").
   * Absent if not set. For PKCS#7 signatures the authoritative time is usually
   * inside the signature blob instead; use this only as a fallback.
   */
  time?: string;
  /**
   * Certification level for a DocMDP signature: 1 = no changes allowed,
   * 2 = form fill-in and signing allowed, 3 = additionally annotations allowed.
   * Absent for an ordinary (non-certifying) signature.
   */
  docMdpPermission?: 1 | 2 | 3;
  /**
   * The /ByteRange array covered by the signature digest: a flat list of
   * (offset, length) pairs, exactly as stored in the PDF. A signature covers
   * the whole file when the last pair ends at the file size — anything less
   * means content was appended after signing.
   */
  byteRange: number[];
  /**
   * Size in bytes of the signature value (/Contents). Read the bytes
   * themselves with {@link PDFiumDocument.getSignatureContents}.
   */
  contentsLength: number;
}

export interface GetSignatureContentsOptions {
  /** Write the signature bytes to this file path instead of returning a Buffer. */
  output?: string;
}

/**
 * A document-level JavaScript action — a script a viewer runs when the
 * document opens.
 */
export interface JavaScriptAction {
  /** 0-based index in the document's /Names /JavaScript tree. */
  index: number;
  /** Entry name in the JavaScript name tree. Empty string if unnamed. */
  name: string;
  /** The script source, returned as inert text. Empty string if unreadable. */
  script: string;
}

/** How a viewer should frame the target page when following a destination. */
export type DestinationView =
  'xyz' | 'fit' | 'fitH' | 'fitV' | 'fitR' | 'fitB' | 'fitBH' | 'fitBV' | 'unknown';

/**
 * A named destination — an anchor that GoTo actions and external links target
 * by name instead of by page number.
 */
export interface NamedDestination {
  /** The destination's name, e.g. 'Chapter2'. */
  name: string;
  /** Target page index. Absent if the destination cannot be resolved to a page. */
  pageIndex?: number;
  /** Fit type declared by the destination. */
  view: DestinationView;
  /**
   * Raw view parameters, up to four numbers whose meaning depends on `view` —
   * for example `fitR` carries [left, bottom, right, top] and `fitH` a single
   * top coordinate. Empty for `fit` and `fitB`, which take none.
   */
  viewParams: number[];
  /** Destination X coordinate. Only set for `xyz` destinations that specify one. */
  destX?: number;
  /** Destination Y coordinate. Only set for `xyz` destinations that specify one. */
  destY?: number;
  /** Destination zoom level. Only set for `xyz` destinations that specify one. */
  destZoom?: number;
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
  getCharacters(start?: number, count?: number): Promise<TextCharacter[]>;
  render(options?: PageRenderOptions): Promise<Buffer | void>;
  getObject(index: number): Promise<PageObject>;
  renderImage(index: number, options?: ImageRenderOptions): Promise<Buffer | void>;
  getLinks(): Promise<Link[]>;
  search(text: string, options?: SearchOptions): Promise<SearchMatch[]>;
  getAnnotations(): Promise<Annotation[]>;
  getAnnotationAttachment(index: number, outputPath?: string): Promise<Buffer | undefined>;
  getFormFields(): Promise<FormField[]>;
  getStructTree(): Promise<StructElement[]>;
  close(): void;
}

export interface NativeDocument {
  readonly pageCount: number;
  readonly metadata: DocumentMetadata;
  getPage(index: number): Promise<NativePage>;
  getBookmarks(): Promise<Bookmark[]>;
  getAttachments(): Promise<Attachment[]>;
  getAttachment(index: number, outputPath?: string): Promise<Buffer | undefined>;
  getSignatures(): Promise<Signature[]>;
  getSignatureContents(index: number, outputPath?: string): Promise<Buffer | undefined>;
  getJavaScriptActions(): Promise<JavaScriptAction[]>;
  getNamedDestinations(): Promise<NamedDestination[]>;
  destroy(): void;
}

export interface SplitDocumentOptions {
  /** Write each part to these file paths instead of returning Buffers. Must have splitAt.length + 1 entries. */
  outputs?: string[];
  /** Password for the source PDF (if encrypted). */
  password?: string;
}

export interface MergeDocumentInput {
  /** PDF file path or Buffer. */
  input: Buffer | string;
  /** Password for this PDF (if encrypted). */
  password?: string;
}

export interface MergeDocumentsOptions {
  /** Write to this file path instead of returning a Buffer. */
  output?: string;
}

export interface AssemblePagesOptions {
  /** Write to this file path instead of returning a Buffer. */
  output?: string;
  /** Password for the source PDF (if encrypted). */
  password?: string;
}

/**
 * A file to embed with {@link addAttachments}.
 *
 * There is deliberately no `mimeType` or `description` field. The MIME type is
 * `/Subtype` on the embedded-file stream and the description is `/Desc` on the
 * filespec dictionary; PDFium's write API reaches neither in the bundled build,
 * and accepting those values only to drop them silently would be worse than not
 * offering them. Note this is an asymmetry with reading: `Attachment.mimeType`
 * is reported when a PDF already carries it.
 */
export interface AttachmentInput {
  /**
   * File name to store in the /EmbeddedFiles name tree, e.g. 'report.xml'.
   * Must be non-empty and not already present in the document.
   */
  name: string;
  /** File contents. */
  data: Buffer;
  /**
   * Creation date as a PDF date string, e.g. 'D:20250101120000Z'. Defaults to
   * the current time, so set it explicitly for reproducible output.
   */
  creationDate?: string;
  /** Modification date as a PDF date string. */
  modDate?: string;
}

export interface AddAttachmentsOptions {
  /** Write to this file path instead of returning a Buffer. */
  output?: string;
  /** Password for the source PDF (if encrypted). */
  password?: string;
}

export interface NUpPagesOptions {
  /** Number of source pages placed side by side across each output page. */
  columns: number;
  /** Number of source pages stacked down each output page. */
  rows: number;
  /** Output page width in points. Defaults to the width of the first source page. */
  width?: number;
  /** Output page height in points. Defaults to the height of the first source page. */
  height?: number;
  /** Write to this file path instead of returning a Buffer. */
  output?: string;
  /** Password for the source PDF (if encrypted). */
  password?: string;
}

export interface NativeAddon {
  loadDocument(input: Buffer | string, password?: string): Promise<NativeDocument>;
  splitDocument(
    input: Buffer | string,
    splitAt: number[],
    options?: { outputs?: string[]; password?: string },
  ): Promise<Buffer[] | undefined>;
  mergeDocuments(
    inputs: Array<Buffer | string | { input: Buffer | string; password?: string }>,
    options?: { output?: string },
  ): Promise<Buffer | undefined>;
  assemblePages(
    input: Buffer | string,
    pages: number[],
    options?: AssemblePagesOptions,
  ): Promise<Buffer | undefined>;
  nUpPages(input: Buffer | string, options: NUpPagesOptions): Promise<Buffer | undefined>;
  addAttachments(
    input: Buffer | string,
    attachments: AttachmentInput[],
    options?: AddAttachmentsOptions,
  ): Promise<Buffer | undefined>;
}
