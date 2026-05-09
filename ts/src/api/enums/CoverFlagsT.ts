export const CoverFlagsT = Object.freeze({
  IS_32BIT: 0x00000001,
  IS_64BIT: 0x00000002,
  IS_VECTOR: 0x00000004,
  HAS_GOAL: 0x00000008,
  HAS_WEIGHT: 0x00000010,
  HAS_LIMIT: 0x00000400,
  HAS_COUNT: 0x00000800,
  IS_COVERED: 0x00001000,
} as const);

export type CoverFlagsTValue = typeof CoverFlagsT[keyof typeof CoverFlagsT];
