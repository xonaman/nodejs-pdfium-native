/** Base error for all PDFium operations. */
export class PDFiumError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.name = 'PDFiumError';
    this.code = code;
  }
}

/** Thrown when the file cannot be found or opened. */
export class PDFiumFileError extends PDFiumError {
  constructor(message: string) {
    super('FILE', message);
    this.name = 'PDFiumFileError';
  }
}

/** Thrown when the input is not a valid PDF or is corrupted. */
export class PDFiumFormatError extends PDFiumError {
  constructor(message: string) {
    super('FORMAT', message);
    this.name = 'PDFiumFormatError';
  }
}

/** Thrown when a password is required or incorrect. */
export class PDFiumPasswordError extends PDFiumError {
  constructor(message: string) {
    super('PASSWORD', message);
    this.name = 'PDFiumPasswordError';
  }
}

/** Thrown when the PDF uses an unsupported security scheme. */
export class PDFiumSecurityError extends PDFiumError {
  constructor(message: string) {
    super('SECURITY', message);
    this.name = 'PDFiumSecurityError';
  }
}

// The native layer tags PDFium's own failures with a "CODE:message" envelope
// (see GetPdfiumErrorMessage in src/napi_helpers.h). Everything else a worker
// reports is free text that routinely contains a colon of its own — "Page index
// out of range: 4", "Parent directory does not exist: /tmp/x" — so only a
// recognized code may be stripped off. Treating any prefix as a code would eat
// the useful half of those messages.
const NATIVE_ERROR_CODES = new Set(['FILE', 'FORMAT', 'PASSWORD', 'SECURITY', 'PAGE', 'UNKNOWN']);

export function parseNativeError(err: unknown): PDFiumError {
  const msg = err instanceof Error ? err.message : String(err);
  const colonIdx = msg.indexOf(':');
  if (colonIdx === -1) return new PDFiumError('UNKNOWN', msg);

  const code = msg.slice(0, colonIdx);
  if (!NATIVE_ERROR_CODES.has(code)) return new PDFiumError('UNKNOWN', msg);

  const text = msg.slice(colonIdx + 1);

  switch (code) {
    case 'FILE':
      return new PDFiumFileError(text);
    case 'FORMAT':
      return new PDFiumFormatError(text);
    case 'PASSWORD':
      return new PDFiumPasswordError(text);
    case 'SECURITY':
      return new PDFiumSecurityError(text);
    default:
      return new PDFiumError(code, text);
  }
}
