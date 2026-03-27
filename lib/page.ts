import type {
  Annotation,
  Link,
  NativePage,
  PageObject,
  PageRenderOptions,
  PageSize,
  SearchMatch,
  SearchOptions,
} from './types.js';

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

  /** Iterates over all page objects (text spans, paths, images, etc.). */
  *objects(): Generator<PageObject> {
    for (let i = 0; i < this.objectCount; i++) {
      yield this.getObject(i);
    }
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
