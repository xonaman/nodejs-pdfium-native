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
   *
   * An entry is `null` when PDFium counted an embedded file but could not load
   * it — a `/EmbeddedFiles` name-tree slot whose file-specification reference
   * points at a missing object, for instance. The array always has one entry
   * per `metadata.attachmentCount`, so array position matches
   * {@link Attachment.index}. Telling a damaged embedded file apart from a
   * document that simply has none matters before a
   * destructive operation: {@link splitDocument} and {@link mergeDocuments}
   * discard the embedded-files tree, and for an e-invoice the embedded XML is
   * often the only machine-readable copy.
   *
   * ```ts
   * const attachments = await doc.getAttachments();
   * if (attachments.some((a) => a === null)) throw new Error('damaged embedded file');
   * const invoice = attachments.find((a) => a?.name === 'factur-x.xml');
   * ```
   *
   * A `null` says only that the entry could not be loaded, not why;
   * {@link getAttachment} on the same index rejects with the underlying
   * failure. Note the converse does not hold — an entry that lists fine can
   * still fail to yield bytes, so a non-`null` entry is not a promise that
   * {@link getAttachment} will succeed.
   *
   * A `null` only appears once PDFium has counted the entry, so damage that
   * breaks the count itself stays invisible: a name tree that fails to parse
   * reports `attachmentCount: 0` and an empty array, indistinguishable from a
   * document with no embedded files.
   */
  getAttachments(): Promise<(Attachment | null)[]> {
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
   *
   * An entry is `null` when PDFium counted a signature but could not load it,
   * so the array always has one entry per `metadata.signatureCount`. A `null`
   * is not "unsigned" — it is a signature this library could not read, which
   * for a document whose signatures are the point is the more alarming of the
   * two.
   */
  getSignatures(): Promise<(Signature | null)[]> {
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
  getJavaScriptActions(): Promise<(JavaScriptAction | null)[]> {
    return withConcurrency(() => this.native.getJavaScriptActions());
  }

  /**
   * Returns the document's named destinations — the anchors that GoTo actions
   * and external links target by name rather than by page number.
   *
   * This resolves a name like `Chapter2` to a concrete page index without
   * walking every link on every page.
   *
   * A destination PDFium cannot resolve is omitted. Unlike the other counted
   * listings this one does not report such an entry, because its null is
   * ambiguous: a legal legacy `/Dests` entry whose value is an indirect
   * reference is counted but not resolved by the index-based lookup, so
   * reporting it would flag healthy documents. See `destinations_worker.h`.
   */
  getNamedDestinations(): Promise<NamedDestination[]> {
    return withConcurrency(() => this.native.getNamedDestinations());
  }
}
