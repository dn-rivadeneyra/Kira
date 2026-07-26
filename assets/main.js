import { invoke, KiraError } from './kira.js';

document.addEventListener('DOMContentLoaded', () => {
    const nameInput = document.getElementById('name-input');
    const greetBtn = document.getElementById('greet-btn');
    const greetResult = document.getElementById('greet-result');

    const unknownBtn = document.getElementById('unknown-btn');
    const unknownResult = document.getElementById('unknown-result');

    greetBtn.addEventListener('click', async () => {
        try {
            const name = nameInput.value.trim() || 'World';
            const res = await invoke('greet', { name });
            greetResult.textContent = JSON.stringify(res, null, 2);
        } catch (err) {
            if (err instanceof KiraError) {
                greetResult.textContent = `[KiraError] Code: ${err.code}\nMessage: ${err.message}`;
            } else {
                greetResult.textContent = `Error: ${err.message || err}`;
            }
        }
    });

    unknownBtn.addEventListener('click', async () => {
        try {
            const res = await invoke('unknown_cmd', {});
            unknownResult.textContent = JSON.stringify(res, null, 2);
        } catch (err) {
            if (err instanceof KiraError) {
                unknownResult.textContent = `[KiraError] Code: ${err.code}\nMessage: ${err.message}`;
            } else {
                unknownResult.textContent = `Error: ${err.message || err}`;
            }
        }
    });
});
