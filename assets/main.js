document.addEventListener('DOMContentLoaded', () => {
    const nameInput = document.getElementById('name-input');
    const greetBtn = document.getElementById('greet-btn');
    const outputBox = document.getElementById('output-box');
    const outputContent = document.getElementById('output-content');

    const infoBtn = document.getElementById('info-btn');
    const infoBox = document.getElementById('info-box');
    const infoContent = document.getElementById('info-content');

    // Helper to check if invoke is available
    function checkInvoke() {
        if (typeof window.invoke !== 'function') {
            console.warn('[Kira Frontend] window.invoke is not yet injected or running outside Kira container.');
            return false;
        }
        return true;
    }

    // Greet command handler
    async function handleGreet() {
        const name = nameInput.value.trim() || 'World';

        if (!checkInvoke()) {
            outputContent.textContent = JSON.stringify({
                status: 'error',
                error: 'window.invoke function is not defined. Run within Kira application.'
            }, null, 2);
            outputBox.classList.remove('hidden');
            return;
        }

        try {
            greetBtn.disabled = true;
            // Call C++ registered command 'greet' with Promise API
            const result = await window.invoke('greet', { name });
            
            outputContent.textContent = JSON.stringify(result, null, 2);
            outputBox.classList.remove('hidden');
        } catch (err) {
            outputContent.textContent = JSON.stringify({
                status: 'error',
                error: err.message || String(err)
            }, null, 2);
            outputBox.classList.remove('hidden');
        } finally {
            greetBtn.disabled = false;
        }
    }

    // System info command handler
    async function handleGetInfo() {
        if (!checkInvoke()) {
            infoContent.textContent = JSON.stringify({
                status: 'error',
                error: 'window.invoke function is not defined.'
            }, null, 2);
            infoBox.classList.remove('hidden');
            return;
        }

        try {
            infoBtn.disabled = true;
            const result = await window.invoke('get_info', {});
            infoContent.textContent = JSON.stringify(result, null, 2);
            infoBox.classList.remove('hidden');
        } catch (err) {
            infoContent.textContent = JSON.stringify({
                status: 'error',
                error: err.message || String(err)
            }, null, 2);
            infoBox.classList.remove('hidden');
        } finally {
            infoBtn.disabled = false;
        }
    }

    // Event Listeners
    greetBtn.addEventListener('click', handleGreet);
    nameInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            handleGreet();
        }
    });

    infoBtn.addEventListener('click', handleGetInfo);
});
