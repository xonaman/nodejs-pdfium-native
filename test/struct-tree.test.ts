import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';
import type { StructElement } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

// struct-tree.pdf (see scripts/generate-fixtures.mjs) tags one page as
//   Document (lang en-US)
//     H1     title "Main heading"        MCID 0
//     P      lang de-DE                  MCID 1
//     Figure alt/actualText/id           MCID 2
const withStructTree = async (fn: (tree: StructElement[]) => void | Promise<void>) => {
  const doc = await loadDocument(fixture('struct-tree.pdf'));
  const page = await doc.getPage(0);
  try {
    await fn(await page.getStructTree());
  } finally {
    page.close();
    doc.destroy();
  }
};

describe('PDFiumPage.getStructTree', () => {
  it('returns an empty array for an untagged page', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    const page = await doc.getPage(0);
    expect(await page.getStructTree()).toEqual([]);
    page.close();
    doc.destroy();
  });

  it('returns an empty array when /MarkInfo is set but no tree exists', async () => {
    // tagged.pdf claims to be tagged but carries no /StructTreeRoot
    const doc = await loadDocument(fixture('tagged.pdf'));
    expect(doc.metadata.isTagged).toBe(true);
    const page = await doc.getPage(0);
    expect(await page.getStructTree()).toEqual([]);
    page.close();
    doc.destroy();
  });

  it('returns the root element with its children nested beneath it', async () => {
    await withStructTree((tree) => {
      expect(tree).toHaveLength(1);

      const [root] = tree;
      expect(root.type).toBe('Document');
      expect(root.objType).toBe('StructElem');
      expect(root.children?.map((c) => c.type)).toEqual(['H1', 'P', 'Figure']);
    });
  });

  it('reports the title of an element', async () => {
    await withStructTree((tree) => {
      const heading = tree[0].children![0];
      expect(heading.type).toBe('H1');
      expect(heading.title).toBe('Main heading');
    });
  });

  it('reports alt text, actual text and id on a figure', async () => {
    await withStructTree((tree) => {
      const figure = tree[0].children!.find((c) => c.type === 'Figure')!;
      expect(figure.altText).toBe('A red square');
      expect(figure.actualText).toBe('Figure 1');
      expect(figure.id).toBe('fig1');
    });
  });

  it('reports a language override on the element that carries it', async () => {
    await withStructTree((tree) => {
      expect(tree[0].lang).toBe('en-US');
      expect(tree[0].children!.find((c) => c.type === 'P')!.lang).toBe('de-DE');
      // the heading has no /Lang of its own
      expect(tree[0].children!.find((c) => c.type === 'H1')!.lang).toBeUndefined();
    });
  });

  it('reports marked content IDs, and omits the key when there is none', async () => {
    await withStructTree((tree) => {
      expect(tree[0].children!.map((c) => c.markedContentId)).toEqual([0, 1, 2]);
      // the Document element wraps others rather than page content
      expect(tree[0].markedContentId).toBeUndefined();
    });
  });

  it('omits optional keys that the element does not set', async () => {
    await withStructTree((tree) => {
      const paragraph = tree[0].children!.find((c) => c.type === 'P')!;
      expect(paragraph.altText).toBeUndefined();
      expect(paragraph.actualText).toBeUndefined();
      expect(paragraph.title).toBeUndefined();
      expect(paragraph.id).toBeUndefined();
      // leaf elements report no children key at all
      expect(paragraph.children).toBeUndefined();
    });
  });

  it('rejects after the page is closed', async () => {
    const doc = await loadDocument(fixture('struct-tree.pdf'));
    const page = await doc.getPage(0);
    page.close();
    await expect(page.getStructTree()).rejects.toThrow('Page is closed');
    doc.destroy();
  });
});
