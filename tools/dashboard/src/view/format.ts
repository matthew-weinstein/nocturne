export interface Part {
  n: string;
  u: string;
}

const formatters = new Map<string, Intl.DateTimeFormat>();

function formatter(zone: string): Intl.DateTimeFormat {
  let found = formatters.get(zone);
  if (found === undefined) {
    found = new Intl.DateTimeFormat("en-GB", {
      timeZone: zone,
      hourCycle: "h23",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
    formatters.set(zone, found);
  }
  return found;
}

export function minuteOfDay(unix: number, zone: string): number {
  let hour = 0;
  let minute = 0;
  let second = 0;
  for (const part of formatter(zone).formatToParts(new Date(unix * 1000))) {
    if (part.type === "hour") {
      hour = Number(part.value);
    } else if (part.type === "minute") {
      minute = Number(part.value);
    } else if (part.type === "second") {
      second = Number(part.value);
    }
  }
  const fraction = unix - Math.floor(unix);
  return hour * 60 + minute + (second + fraction) / 60;
}

export function clockPartsOfMinute(minuteValue: number): Part {
  const wrapped = ((minuteValue % 1440) + 1440) % 1440;
  const hour = Math.floor(wrapped / 60);
  const minute = Math.floor(wrapped % 60);
  let hour12 = hour % 12;
  if (hour12 === 0) {
    hour12 = 12;
  }
  let suffix = "PM";
  if (hour < 12) {
    suffix = "AM";
  }
  return { n: `${hour12}:${String(minute).padStart(2, "0")}`, u: suffix };
}

export function clockOfMinute(minuteValue: number): string {
  const parts = clockPartsOfMinute(minuteValue);
  return `${parts.n} ${parts.u}`;
}

export function clockText(unix: number | null, zone: string): string {
  if (unix === null) {
    return "—";
  }
  return clockOfMinute(minuteOfDay(unix, zone));
}

export function lengthText(ms: number): string {
  const seconds = Math.round(ms / 1000);
  if (seconds < 60) {
    return `${seconds}s`;
  }
  const minutes = Math.floor(seconds / 60);
  const rest = seconds % 60;
  if (minutes >= 60) {
    const hours = Math.floor(minutes / 60);
    const leftover = minutes % 60;
    if (leftover === 0) {
      return `${hours}h`;
    }
    return `${hours}h ${leftover}m`;
  }
  if (rest === 0) {
    return `${minutes}m`;
  }
  return `${minutes}m ${rest}s`;
}

export function durationParts(minutes: number): Part[] {
  const total = Math.round(minutes);
  const hours = Math.floor(total / 60);
  const rest = total % 60;
  if (hours === 0) {
    return [{ n: String(rest), u: "m" }];
  }
  return [{ n: String(hours), u: "h" }, { n: String(rest), u: "m" }];
}

export function durationText(minutes: number | null): string {
  if (minutes === null) {
    return "—";
  }
  const words: string[] = [];
  for (const part of durationParts(minutes)) {
    words.push(`${part.n}${part.u}`);
  }
  return words.join(" ");
}

const WEEKDAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

export function nightDateText(isoDate: string): string {
  const [year, month, day] = isoDate.split("-").map(Number);
  const weekday = new Date(Date.UTC(year, month - 1, day)).getUTCDay();
  return `${WEEKDAYS[weekday]} ${day} ${MONTHS[month - 1]}`;
}

export function countText(count: number | null, none = "None"): string {
  if (count === null) {
    return "—";
  }
  if (count === 0) {
    return none;
  }
  return String(count);
}

export function percentText(fraction: number | null): string {
  if (fraction === null) {
    return "—";
  }
  return `${Math.round(fraction * 100)}%`;
}
