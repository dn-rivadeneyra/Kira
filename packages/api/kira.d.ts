export class KiraError extends Error {
    readonly code: string;
    constructor(code: string, message: string);
}

export interface InvokeOptions {
    timeoutMs?: number;
}

export function invoke<T = unknown>(
    command: string,
    payload?: unknown,
    options?: InvokeOptions
): Promise<T>;
