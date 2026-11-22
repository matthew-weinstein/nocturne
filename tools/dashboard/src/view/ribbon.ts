export const STEP_MIN = 0.25;
export const HALF_WIDTH = 13;
export const END_WIDTH = 2.6;

const BIN_MIN = 0.5;
const MIN_RANGE_DB = 15;
const TAPER_MIN = 6;

function percentile(sorted: number[], fraction: number): number {
  const index = Math.min(sorted.length - 1, Math.floor(sorted.length * fraction));
  return sorted[index];
}

function ascending(values: number[]): number[] {
  const copy = values.slice();
  copy.sort((a, b) => a - b);
  return copy;
}

export function filledLevels(peaks: (number | null)[]): number[] {
  const known: number[] = [];
  for (const peak of peaks) {
    if (peak !== null) {
      known.push(peak);
    }
  }
  if (known.length === 0) {
    return peaks.map(() => 0);
  }
  const floor = percentile(ascending(known), 0.1);
  const filled: number[] = [];
  for (const peak of peaks) {
    if (peak === null) {
      filled.push(floor);
    } else {
      filled.push(peak);
    }
  }
  return filled;
}

function binLevels(db: number[]): number[] {
  const perBin = Math.round(BIN_MIN / STEP_MIN);
  const sorted = ascending(db);
  const floor = percentile(sorted, 0.1);
  const range = Math.max(MIN_RANGE_DB, sorted[sorted.length - 1] - floor);

  const peaks: number[] = [];
  for (let index = 0; index < db.length; index += perBin) {
    let peak = -Infinity;
    for (let offset = 0; offset < perBin && index + offset < db.length; offset++) {
      peak = Math.max(peak, db[index + offset]);
    }
    peaks.push(peak);
  }

  const ranked = ascending(peaks);
  const raw: number[] = [];
  for (const peak of peaks) {
    let rank = 0;
    if (ranked.length > 1) {
      rank = ranked.indexOf(peak) / (ranked.length - 1);
    }
    const absolute = Math.min(1, Math.max(0, (peak - floor) / range));
    raw.push(Math.min(1, 0.35 * rank + 0.65 * Math.pow(absolute, 0.55)));
  }

  const smoothed: number[] = [];
  for (let index = 0; index < raw.length; index++) {
    let sum = 0;
    let weights = 0;
    let peak = 0;
    for (let offset = -12; offset <= 12; offset++) {
      const value = raw[index + offset];
      if (value === undefined) {
        continue;
      }
      const weight = Math.exp(-0.5 * (offset / 5.5) ** 2);
      sum += value * weight;
      weights += weight;
      peak = Math.max(peak, value * Math.exp(-0.5 * (offset / 4.5) ** 2));
    }
    smoothed.push(Math.min(1, 0.6 * sum / weights + 0.4 * peak));
  }

  const spread = ascending(smoothed);
  const low = percentile(spread, 0.15);
  const high = percentile(spread, 0.995);
  const span = Math.max(1e-6, high - low);
  const levels: number[] = [];
  for (const value of smoothed) {
    levels.push(Math.pow(Math.min(1, Math.max(0, (value - low) / span)), 1.5));
  }
  return levels;
}

function taper(minute: number, spanMin: number): number {
  const edge = Math.max(0, Math.min(1, minute / TAPER_MIN, (spanMin - minute) / TAPER_MIN));
  return edge * edge * (3 - 2 * edge);
}

/** Half-widths of the night's ribbon every STEP_MIN from bed to up, from 15 s peak levels in dB. */
export function ribbonWidths(db: number[], spanMin: number): number[] {
  const steps: number[] = [];
  for (let minute = 0; minute <= spanMin + 0.001; minute += STEP_MIN) {
    steps.push(Math.min(minute, spanMin));
  }
  if (db.length === 0) {
    return steps.map(() => END_WIDTH);
  }

  const levels = binLevels(db);
  const level = (minute: number): number => {
    if (levels.length === 1) {
      return levels[0];
    }
    const position = minute / BIN_MIN;
    const index = Math.min(levels.length - 2, Math.max(0, Math.floor(position)));
    const fraction = Math.min(1, Math.max(0, position - index));
    return levels[index] + (levels[index + 1] - levels[index]) * fraction;
  };

  const raw: number[] = [];
  for (const minute of steps) {
    raw.push(END_WIDTH + (HALF_WIDTH - END_WIDTH) * Math.pow(level(minute), 0.6));
  }

  const widths: number[] = [];
  for (let index = 0; index < steps.length; index++) {
    let sum = 0;
    let weights = 0;
    for (let offset = -30; offset <= 30; offset++) {
      const value = raw[index + offset];
      if (value === undefined) {
        continue;
      }
      const weight = Math.exp(-0.5 * (offset / 11) ** 2);
      sum += value * weight;
      weights += weight;
    }
    widths.push(END_WIDTH + (sum / weights - END_WIDTH) * taper(steps[index], spanMin));
  }
  return widths;
}
