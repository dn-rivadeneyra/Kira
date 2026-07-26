export class KiraError extends Error {
    constructor(code, message) {
        super(message || 'Kira IPC Error');
        this.name = 'KiraError';
        this.code = code || 'internal_error';
    }
}

const pendingRequests = new Map();
let requestCounter = 0;

function getNativeBootstrap() {
    if (typeof window !== 'undefined' && window.__KIRA_INTERNAL__) {
        return window.__KIRA_INTERNAL__;
    }
    return null;
}

// Initialize listener on native bridge
if (typeof window !== 'undefined') {
    const initListener = () => {
        const bootstrap = getNativeBootstrap();
        if (bootstrap && typeof bootstrap.onMessage === 'function') {
            bootstrap.onMessage((rawMsg) => {
                try {
                    const data = typeof rawMsg === 'string' ? JSON.parse(rawMsg) : rawMsg;
                    if (!data || data.version !== 1) return;

                    if (data.type === 'result' && data.id && pendingRequests.has(data.id)) {
                        const entry = pendingRequests.get(data.id);
                        pendingRequests.delete(data.id);
                        if (entry.timer) clearTimeout(entry.timer);

                        if (data.ok) {
                            entry.resolve(data.value);
                        } else {
                            const errObj = data.error || {};
                            entry.reject(new KiraError(errObj.code || 'internal_error', errObj.message || 'Command failed'));
                        }
                    } else if (data.type === 'protocol_error' && data.id && pendingRequests.has(data.id)) {
                        const entry = pendingRequests.get(data.id);
                        pendingRequests.delete(data.id);
                        if (entry.timer) clearTimeout(entry.timer);

                        const errObj = data.error || {};
                        entry.reject(new KiraError(errObj.code || 'invalid_json', errObj.message || 'Protocol Error'));
                    }
                } catch (e) {
                    console.error('[Kira API] Listener error parsing message:', e);
                }
            });
        }
    };

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initListener);
    } else {
        initListener();
    }
}

export function invoke(command, payload = {}, options = {}) {
    return new Promise((resolve, reject) => {
        const bootstrap = getNativeBootstrap();
        if (!bootstrap || typeof bootstrap.send !== 'function') {
            return reject(new KiraError('internal_error', 'Kira native transport is unavailable'));
        }

        const timeoutMs = options.timeoutMs !== undefined ? options.timeoutMs : 30000;
        if (timeoutMs <= 0) {
            return reject(new KiraError('invalid_payload', 'Timeout value must be greater than zero'));
        }

        const id = 'kira_req_' + (++requestCounter) + '_' + Date.now() + '_' + Math.random().toString(36).substr(2, 6);

        let timer = null;
        if (timeoutMs > 0) {
            timer = setTimeout(() => {
                if (pendingRequests.has(id)) {
                    pendingRequests.delete(id);
                    reject(new KiraError('request_timeout', `Request '${command}' timed out after ${timeoutMs}ms`));
                }
            }, timeoutMs);
        }

        pendingRequests.set(id, { resolve, reject, timer });

        const requestMsg = JSON.stringify({
            version: 1,
            type: 'invoke',
            id: id,
            command: command,
            payload: payload || {}
        });

        bootstrap.send(requestMsg);
    });
}
