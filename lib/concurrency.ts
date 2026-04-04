import { availableParallelism } from 'node:os';
import pLimit, { type LimitFunction } from 'p-limit';

let limit: LimitFunction = pLimit(availableParallelism());

/**
 * Gets or sets the maximum number of concurrent native operations.
 *
 * The default value is the number of CPU cores (`os.availableParallelism()`).
 * A value of `0` resets to the default.
 */
export function concurrency(value?: number): number {
  if (value !== undefined) {
    if (!Number.isInteger(value) || value < 0) {
      throw new TypeError('Concurrency must be a non-negative integer');
    }
    limit = pLimit(value === 0 ? availableParallelism() : value);
  }
  return limit.concurrency;
}

export function withConcurrency<T>(fn: () => Promise<T>): Promise<T> {
  return limit(fn);
}
