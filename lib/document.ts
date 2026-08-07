import { withConcurrency } from './concurrency.js';
import { PDFiumPage } from './page.js';
import { isNativeIndex } from './validate.js';
import type {
  Attachment,
  Bookmark,
  DocumentMetadata,
  GetAttachmentOptions,
  GetSignatureContentsOptions,
  JavaScriptAction,
  NamedDestination,
  NativeDocument,
  Signature,
} from './types.js';

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
    return new PDFiumPage(await withConcurrency(() => this.native.getPage(index)));
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
  getBookmarks(): Promise<Bookmark[]> {
    return withConcurrency(() => this.native.getBookmarks());
  }

  /**
   * Returns metadata for every embedded file (attachment) in the document —
   * name, MIME type and dates — without reading the file bytes.
   *
   * For ZUGFeRD / Factur-X / XRechnung PDF/A-3 e-invoices, the structured
   * invoice lives here (e.g. `factur-x.xml`); pass its `index` to
   * {@link getAttachment} to read the bytes.
   */
  getAttachments(): Promise<Attachment[]> {
    return withConcurrency(() => this.native.getAttachments());
  }

  /** Writes the attachment at `index` to a file path. */
  getAttachment(index: number, options: GetAttachmentOptions & { output: string }): Promise<void>;
  /**
   * Reads the raw bytes of the attachment at `index`.
   *
   * One PDFium quirk to know about: an embedded file that decodes to *zero*
   * bytes comes back as its raw compressed stream instead of as an empty
   * buffer. PDFium treats "decoded to empty" as a decode failure and falls back
   * to the undecoded bytes, so a zero-length FlateDecode attachment yields the
   * 8-byte zlib envelope rather than nothing.
   */
  getAttachment(index: number, options?: GetAttachmentOptions): Promise<Buffer>;
  getAttachment(index: number, options?: GetAttachmentOptions): Promise<Buffer | void> {
    // Reject indices the native ToInt32 coercion would silently alter (see
    // isNativeIndex): a NaN/Infinity/fractional index (e.g. from
    // `Number(userInput)`), or one outside the 32-bit range, must not wrap onto
    // a valid, different attachment and return the wrong embedded file. In-range
    // integer out-of-range is still reported natively.
    if (!isNativeIndex(index)) {
      return Promise.reject(
        new RangeError(`Attachment index must be a 32-bit integer, got ${index}`),
      );
    }
    return withConcurrency(() => this.native.getAttachment(index, options?.output));
  }

  /**
   * Returns metadata for every digital signature in the document — encoding,
   * reason, signing time, certification level and the byte ranges the digest
   * covers.
   *
   * Nothing is cryptographically verified: this reports what the PDF declares
   * about itself. To actually validate a signature, read its blob with
   * {@link getSignatureContents} and check it against `byteRange`.
   */
  getSignatures(): Promise<Signature[]> {
    return withConcurrency(() => this.native.getSignatures());
  }

  /** Writes the signature's /Contents bytes at `index` to a file path. */
  getSignatureContents(
    index: number,
    options: GetSignatureContentsOptions & { output: string },
  ): Promise<void>;
  /** Reads the raw /Contents bytes (PKCS#1 / PKCS#7 DER) of the signature at `index`. */
  getSignatureContents(index: number, options?: GetSignatureContentsOptions): Promise<Buffer>;
  getSignatureContents(
    index: number,
    options?: GetSignatureContentsOptions,
  ): Promise<Buffer | void> {
    // Same 32-bit guard as getAttachment: a fractional or out-of-range index
    // must not ToInt32-wrap onto a different, valid signature.
    if (!isNativeIndex(index)) {
      return Promise.reject(
        new RangeError(`Signature index must be a 32-bit integer, got ${index}`),
      );
    }
    return withConcurrency(() => this.native.getSignatureContents(index, options?.output));
  }

  /**
   * Returns the document-level JavaScript actions — the scripts a viewer runs
   * when the document opens.
   *
   * Useful for inspection and triage: a PDF from an untrusted source that
   * carries document-open JavaScript is worth checking before rendering.
   * Nothing here is executed; the scripts come back as inert text.
   */
  getJavaScriptActions(): Promise<JavaScriptAction[]> {
    return withConcurrency(() => this.native.getJavaScriptActions());
  }

  /**
   * Returns the document's named destinations — the anchors that GoTo actions
   * and external links target by name rather than by page number.
   *
   * This resolves a name like `Chapter2` to a concrete page index without
   * walking every link on every page.
   */
  getNamedDestinations(): Promise<NamedDestination[]> {
    return withConcurrency(() => this.native.getNamedDestinations());
  }
}
