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

describe('PDFiumPage.getStructTree resource bounds', () => {
  it('bounds a structure tree that expands exponentially', async () => {
    // struct-tree-bomb.pdf is ~1.2 KB but its /K arrays each name the same
    // child twice. PDFium resolves every matching kid slot, so flattening the
    // DAG into a tree doubles the node count per level — 17 links would be
    // 262,143 elements. Without a node budget this allocates unboundedly while
    // holding the global PDFium mutex, stalling every other call in the process.
    const doc = await loadDocument(fixture('struct-tree-bomb.pdf'));
    const page = await doc.getPage(0);

    const started = Date.now();
    const tree = await page.getStructTree();
    const elapsed = Date.now() - started;

    const total = (nodes: StructElement[]): number =>
      nodes.reduce((sum, n) => sum + 1 + total(n.children ?? []), 0);
    const count = total(tree);

    // capped rather than 2^18 - 1, and fast enough to prove it did not expand
    expect(count).toBeGreaterThan(0);
    expect(count).toBeLessThanOrEqual(100_000);
    expect(elapsed).toBeLessThan(10_000);

    page.close();
    doc.destroy();
  });
});
