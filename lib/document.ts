import { withConcurrency } from './concurrency.js';
import { PDFiumPage } from './page.js';
import { isNativeIndex } from './validate.js';
import type {
  Attachment,
  Bookmark,
  DocumentMetadata,
  GetAttachmentOptions,
  NativeDocument,
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
  /** Reads the raw bytes of the attachment at `index`. */
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
}
