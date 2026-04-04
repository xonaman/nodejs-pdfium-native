import { withConcurrency } from './concurrency.js';
import type {
  Annotation,
  FormField,
  ImagePageObject,
  ImageRenderOptions,
  Link,
  NativePage,
  PageObject,
  PageObjectBounds,
  PageRenderOptions,
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
  /** 0-based page index. */
  readonly number: number;
  /** Number of page objects (text spans, paths, images, etc.). */
  readonly objectCount: number;
  /** Page rotation: 0 = none, 1 = 90° CW, 2 = 180°, 3 = 270° CW. */
  readonly rotation: number;
  /** Whether the page has transparency. */
  readonly hasTransparency: boolean;
  /** Page label (e.g. 'i', 'ii', '1', '2'). Only set if the document defines page labels. */
  readonly label?: string;
  /** Crop box (visible region), if explicitly set. */
  readonly cropBox?: PageObjectBounds;
  /** Trim box (intended finished page size), if explicitly set. */
  readonly trimBox?: PageObjectBounds;

  /** @internal */
  constructor(private native: NativePage) {
    this.width = native.width;
    this.height = native.height;
    this.number = native.number;
    this.objectCount = native.objectCount;
    this.rotation = native.rotation;
    this.hasTransparency = native.hasTransparency;
    if (native.label !== undefined) this.label = native.label;
    if (native.cropBox !== undefined) this.cropBox = native.cropBox;
    if (native.trimBox !== undefined) this.trimBox = native.trimBox;
  }

  /** Extracts all text from the page. */
  getText(): Promise<string> {
    return withConcurrency(() => this.native.getText());
  }

  /** Renders the page to a file. */
  render(options: PageRenderOptions & { output: string }): Promise<void>;
  /** Renders the page to an encoded image buffer (JPEG or PNG). */
  render(options?: PageRenderOptions): Promise<Buffer>;
  render(options?: PageRenderOptions): Promise<Buffer | void> {
    return withConcurrency(() => this.native.render(options));
  }

  /** Returns the page object at the given index with type and bounds. */
  async getObject(index: number): Promise<PageObject> {
    const obj = await withConcurrency(() => this.native.getObject(index));
    // wrap the native render function on image objects with the concurrency semaphore
    if (obj.type === 'image') {
      const nativeRender = (obj as ImagePageObject).render.bind(obj);
      (obj as ImagePageObject).render = ((opts?: ImageRenderOptions) =>
        withConcurrency(() => nativeRender(opts))) as ImagePageObject['render'];
    }
    return obj;
  }

  /** Iterates over all page objects (text spans, paths, images, etc.). */
  async *objects(): AsyncGenerator<PageObject> {
    for (let i = 0; i < this.objectCount; i++) {
      yield this.getObject(i);
    }
  }

  /** Closes the page and frees resources. */
  close(): void {
    this.native.close();
  }

  /** Returns all links on the page. */
  getLinks(): Promise<Link[]> {
    return withConcurrency(() => this.native.getLinks());
  }

  /** Searches for text on the page. Returns matches with positions and bounding rects. */
  search(text: string, options?: SearchOptions): Promise<SearchMatch[]> {
    return withConcurrency(() => this.native.search(text, options));
  }

  /** Returns all annotations on the page. */
  getAnnotations(): Promise<Annotation[]> {
    return withConcurrency(() => this.native.getAnnotations());
  }

  /** Returns all form fields on the page. */
  getFormFields(): Promise<FormField[]> {
    return withConcurrency(() => this.native.getFormFields());
  }
}
