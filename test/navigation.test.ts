import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

// Exact scripts embedded by scripts/generate-fixtures.mjs.
const OPEN_ACTION_JS = 'app.alert("Opened");';
const HELPER_JS = 'function helper() { return 42; }';

describe('PDFiumDocument.getJavaScriptActions', () => {
  it('returns an empty array for a document without JavaScript', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    expect(await doc.getJavaScriptActions()).toEqual([]);
    doc.destroy();
  });

  it('lists each action with its name and script', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const actions = await doc.getJavaScriptActions();

    expect(actions).toHaveLength(2);
    expect(actions.map((a) => a.index)).toEqual([0, 1]);

    const open = actions.find((a) => a.name === 'OpenAction')!;
    expect(open.script).toBe(OPEN_ACTION_JS);

    const helper = actions.find((a) => a.name === 'Helper')!;
    expect(helper.script).toBe(HELPER_JS);

    doc.destroy();
  });

  it('rejects after the document is destroyed', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    doc.destroy();
    await expect(doc.getJavaScriptActions()).rejects.toThrow('Document is destroyed');
  });
});

describe('PDFiumDocument.getNamedDestinations', () => {
  it('returns an empty array for a document without destinations', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    expect(await doc.getNamedDestinations()).toEqual([]);
    doc.destroy();
  });

  it('resolves each destination name to a page index', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const dests = await doc.getNamedDestinations();

    expect(dests.map((d) => d.name).sort()).toEqual(['Alpha', 'Beta', 'Legacy']);
    expect(dests.find((d) => d.name === 'Alpha')!.pageIndex).toBe(0);
    expect(dests.find((d) => d.name === 'Beta')!.pageIndex).toBe(1);

    doc.destroy();
  });

  it('reads destinations from the legacy /Dests catalog dictionary too', async () => {
    // 'Legacy' lives in the old-style catalog /Dests dict, not the name tree
    const doc = await loadDocument(fixture('navigation.pdf'));
    const legacy = (await doc.getNamedDestinations()).find((d) => d.name === 'Legacy')!;

    expect(legacy.pageIndex).toBe(2);
    expect(legacy.view).toBe('fitH');
    expect(legacy.viewParams).toEqual([250]);

    doc.destroy();
  });

  it('reports the fit type and its parameters', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const dests = await doc.getNamedDestinations();

    const alpha = dests.find((d) => d.name === 'Alpha')!;
    expect(alpha.view).toBe('xyz');
    expect(alpha.viewParams).toEqual([50, 700, 2]);

    // 'fit' takes no parameters at all
    const beta = dests.find((d) => d.name === 'Beta')!;
    expect(beta.view).toBe('fit');
    expect(beta.viewParams).toEqual([]);

    doc.destroy();
  });

  it('exposes x/y/zoom only for xyz destinations that specify them', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const dests = await doc.getNamedDestinations();

    const alpha = dests.find((d) => d.name === 'Alpha')!;
    expect(alpha.destX).toBe(50);
    expect(alpha.destY).toBe(700);
    expect(alpha.destZoom).toBe(2);

    const beta = dests.find((d) => d.name === 'Beta')!;
    expect(beta.destX).toBeUndefined();
    expect(beta.destY).toBeUndefined();
    expect(beta.destZoom).toBeUndefined();

    doc.destroy();
  });

  it('lists every destination the document counts', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const dests = await doc.getNamedDestinations();

    // for a document PDFium can resolve by index, the listing is complete
    expect(doc.metadata.namedDestinationCount).toBe(3);
    expect(dests).toHaveLength(doc.metadata.namedDestinationCount);

    doc.destroy();
  });

  it('rejects after the document is destroyed', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    doc.destroy();
    await expect(doc.getNamedDestinations()).rejects.toThrow('Document is destroyed');
  });
});

describe('PDFiumDocument.getNamedDestination', () => {
  it('resolves a name from the /Names /Dests name tree', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const alpha = await doc.getNamedDestination('Alpha');

    expect(alpha).not.toBeNull();
    expect(alpha!.name).toBe('Alpha');
    expect(alpha!.pageIndex).toBe(0);
    expect(alpha!.view).toBe('xyz');
    expect(alpha!.viewParams).toEqual([50, 700, 2]);
    expect(alpha!.destX).toBe(50);
    expect(alpha!.destY).toBe(700);
    expect(alpha!.destZoom).toBe(2);

    doc.destroy();
  });

  it('resolves a name from the legacy /Dests catalog dictionary', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const legacy = await doc.getNamedDestination('Legacy');

    expect(legacy!.pageIndex).toBe(2);
    expect(legacy!.view).toBe('fitH');
    expect(legacy!.viewParams).toEqual([250]);

    doc.destroy();
  });

  it('agrees with the listing entry for the same name', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const listed = await doc.getNamedDestinations();

    for (const entry of listed) {
      expect(await doc.getNamedDestination(entry.name)).toEqual(entry);
    }

    doc.destroy();
  });

  it('resolves to null for a name the document does not carry', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    // absence is an answer, not an error
    expect(await doc.getNamedDestination('NoSuchAnchor')).toBeNull();
    expect(await doc.getNamedDestination('')).toBeNull();
    doc.destroy();
  });

  it('rejects a non-string name instead of coercing it', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    // @ts-expect-error deliberately malformed input
    expect(() => doc.getNamedDestination(0)).toThrow(TypeError);
    doc.destroy();
  });

  it('rejects after the document is destroyed', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    doc.destroy();
    await expect(doc.getNamedDestination('Alpha')).rejects.toThrow('Document is destroyed');
  });
});

// Regression tests for the gap this listing cannot close by itself.
// indirect-legacy-dests.pdf carries one /Names /Dests entry and three legacy
// catalog /Dests entries whose values are indirect references. Nothing about
// it is malformed — ISO 32000-1 allows an indirect reference wherever a direct
// object is allowed — but FPDF_GetNamedDest does not dereference on the legacy
// path, so it returns neither a destination nor a name for those three. They
// cannot even be reported as null entries, because the index carries no name
// to report them under.
describe('PDFiumDocument.getNamedDestinations with indirect legacy /Dests', () => {
  it('comes back shorter than the count, which is what makes the gap visible', async () => {
    const doc = await loadDocument(fixture('indirect-legacy-dests.pdf'));
    const dests = await doc.getNamedDestinations();

    expect(doc.metadata.namedDestinationCount).toBe(4);
    expect(dests).toHaveLength(1);
    // the one that lists is the name-tree entry; the tree path dereferences
    expect(dests[0]!.name).toBe('Tree');

    // without the count there is nothing to compare against, and a document
    // with four anchors reads as one with a single anchor
    expect(dests.length).toBeLessThan(doc.metadata.namedDestinationCount);

    doc.destroy();
  });

  it('resolves every unlistable destination by name', async () => {
    const doc = await loadDocument(fixture('indirect-legacy-dests.pdf'));

    // none of these appear in getNamedDestinations(), yet all three resolve
    const alpha = await doc.getNamedDestination('Alpha');
    expect(alpha!.pageIndex).toBe(0);
    expect(alpha!.view).toBe('xyz');

    const beta = await doc.getNamedDestination('Beta');
    expect(beta!.pageIndex).toBe(1);
    expect(beta!.view).toBe('fit');

    const gamma = await doc.getNamedDestination('Gamma');
    expect(gamma!.pageIndex).toBe(2);
    expect(gamma!.view).toBe('fitH');
    expect(gamma!.viewParams).toEqual([500]);

    doc.destroy();
  });

  it('reaches by name every name the listing omitted', async () => {
    const doc = await loadDocument(fixture('indirect-legacy-dests.pdf'));
    const listed = (await doc.getNamedDestinations()).map((d) => d.name);

    const resolved = await Promise.all(
      ['Tree', 'Alpha', 'Beta', 'Gamma'].map((n) => doc.getNamedDestination(n)),
    );
    // every anchor the document counts is reachable, listed or not
    expect(resolved.filter((d) => d !== null)).toHaveLength(doc.metadata.namedDestinationCount);
    expect(listed).toEqual(['Tree']);

    doc.destroy();
  });
});

// A document-open script that cannot be decoded is more interesting to a
// triage caller than no script at all, so it is reported as a null entry
// rather than dropped.
describe('PDFiumDocument.getJavaScriptActions with an unloadable action', () => {
  it('reports a null for every action that fails, including a trailing one', async () => {
    const doc = await loadDocument(fixture('dangling-javascript.pdf'));

    const actions = await doc.getJavaScriptActions();

    // two of the three fail, and one of them is the last entry — a trailing
    // failure is the shape most likely to be lost, since the array length
    // comes from the allocation rather than from the final assignment
    expect(actions).toHaveLength(3);
    expect(actions[1]).toBeNull();
    expect(actions[2]).toBeNull();

    expect(actions[0]).toBeTruthy();
    expect(actions[0]!.index).toBe(0);
    expect(actions[0]!.name).toBe('script_alpha');

    // "no scripts" and "scripts I could not read" are distinguishable
    expect(actions.filter((a) => a === null)).toHaveLength(2);

    doc.destroy();
  });

  it('leaves an intact document free of null entries', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const actions = await doc.getJavaScriptActions();
    expect(actions.length).toBeGreaterThan(0);
    expect(actions.every((a) => a !== null)).toBe(true);
    doc.destroy();
  });
});
