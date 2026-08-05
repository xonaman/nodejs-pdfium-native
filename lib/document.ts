import { withConcurrency } from './concurrency.js';
import { PDFiumPage } from './page.js';
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
    // Guard non-integer indices here: the native layer coerces with ToInt32, so
    // a NaN/Infinity/fractional index (e.g. from `Number(userInput)`) would
    // otherwise silently alias onto a valid attachment and return the wrong
    // embedded file. Integer out-of-range is still handled natively.
    if (!Number.isInteger(index)) {
      return Promise.reject(new RangeError(`Attachment index must be an integer, got ${index}`));
    }
    return withConcurrency(() => this.native.getAttachment(index, options?.output));
  }
}
