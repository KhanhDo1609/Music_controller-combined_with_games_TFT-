// Runs once under macOS JavaScript for Automation.
ObjC.import("Foundation");

function readPlayer(commands, recoverNext = false) {
    if (location.origin !== "https://music.youtube.com") {
        return JSON.stringify({error: "Open YouTube Music in Chrome."});
    }
    const bar = document.querySelector("ytmusic-player-bar");
    const findVideo = () => document.querySelector("#movie_player video") ?? document.querySelector("video");
    const video = findVideo();
    const slider = bar?.querySelector("#volume-slider");
    const volumeStep = 5;
    function getTrack() {
        return navigator.mediaSession?.metadata?.title ||
            bar?.querySelector(".title")?.textContent?.trim() || "";
    }

    function readVolume() {
        const value = slider?.getAttribute("aria-valuenow");
        return value != null ? Number(value) : video ? Math.round(video.volume * 100) : null;
    }
    const selectors = {
        PREVIOUS: ".previous-button",
        NEXT: ".next-button",
        PLAY_PAUSE: "#play-pause-button",
        SHUFFLE: ".shuffle",
        REPEAT: ".repeat",
        MUTE: ".volume",
    };
    let unavailable = "";
    let nextWasAudible = false;
    let requestedVolume = readVolume();
    for (const command of commands) {
        if (command === "LOUDER" || command === "QUIETER") {
            if (!Number.isFinite(requestedVolume)) {
                unavailable = command;
                continue;
            }
            const louder = command === "LOUDER";
            const delta = louder ? volumeStep : -volumeStep;
            const target = Math.max(0, Math.min(100, requestedVolume + delta));
            if (slider) {
                // The slider's arrow keys move by one point each.
                const key = louder ? "ArrowRight" : "ArrowLeft";
                const keyCode = louder ? 39 : 37;
                for (let step = 0; step < Math.abs(target - requestedVolume); step++) {
                    slider.dispatchEvent(new KeyboardEvent("keydown", {key, keyCode, cancelable: true}));
                }
            } else if (video) {
                video.volume = target / 100;
            }
            requestedVolume = target;
            continue;
        }
        const control = bar?.querySelector(selectors[command]);
        const button = control?.querySelector("button") ?? control;
        if (button && !button.disabled && button.getAttribute("aria-disabled") !== "true") {
            if (command === "NEXT") nextWasAudible = video?.muted === false && readVolume() > 0;
            button.click();
        } else if (command === "MUTE" && video) {
            video.muted = !video.muted;
        } else {
            unavailable = command;
        }
    }

    const activeVideo = findVideo();
    const muteAction = bar?.querySelector(".volume button")?.getAttribute("aria-label");
    // Chrome can mute the new media element while YouTube Music still reports
    // that its player is unmuted. Leave intentional player mutes alone.
    if (recoverNext && activeVideo?.muted && muteAction === "Mute" && readVolume() > 0) {
        activeVideo.muted = false;
    }

    const metadata = navigator.mediaSession?.metadata;
    const title = getTrack();
    const artist = metadata?.artist || bar?.querySelector(".byline a")?.textContent?.trim() || "";
    const cover = bar?.querySelector("#thumbnail img");
    const art = metadata?.artwork?.[0]?.src || cover?.currentSrc || cover?.src || "";
    const repeat = { NONE: "0", ALL: "A", ONE: "1" }[bar?.getAttribute("repeat-mode")] ?? "?";
    const shuffle = bar?.hasAttribute("shuffle-on") ? "1" : bar?.querySelector(".shuffle") ? "0" : "?";
    const volume = readVolume();
    return JSON.stringify({
        title, artist, art, repeat, shuffle, volume, unavailable, nextWasAudible,
        playing: activeVideo && title ? !activeVideo.paused && !activeVideo.ended : null,
        muted: activeVideo ? activeVideo.muted : null,
    });
}

function run() {
    const input = $.NSFileHandle.fileHandleWithStandardInput;
    const output = $.NSFileHandle.fileHandleWithStandardOutput;
    const chrome = Application("com.google.Chrome");
    let musicTab = null;
    let recoverUntil = 0;
    let buffer = "";

    function snapshot(commands) {
        if (!chrome.running()) {
            musicTab = null;
            return JSON.stringify({error: "Open Chrome and start a song in YouTube Music."});
        }
        const prefix = "https://music.youtube.com/";
        if (musicTab) {
            try {
                if (!musicTab.url().startsWith(prefix)) musicTab = null;
            } catch (error) {
                musicTab = null;
            }
        }
        if (!musicTab) {
            recoverUntil = 0;
            for (const window of chrome.windows()) {
                musicTab = window.tabs().find(tab => tab.url().startsWith(prefix)) ?? null;
                if (musicTab) break;
            }
        }
        if (!musicTab) return JSON.stringify({error: "Open music.youtube.com and start a song."});
        const explicitAudioChange = commands.some(
            command => ["MUTE", "LOUDER", "QUIETER", "PREVIOUS"].includes(command)
        );
        if (explicitAudioChange) recoverUntil = 0;
        const source = "(" + readPlayer.toString() + ")(" + JSON.stringify(commands) +
            "," + (Date.now() < recoverUntil) + ")";
        const state = JSON.parse(chrome.execute(musicTab, {javascript: source}));
        if (state.nextWasAudible && !explicitAudioChange) recoverUntil = Date.now() + 8000;
        return JSON.stringify(state);
    }

    while (true) {
        const data = input.availableData;
        if (!data.length) return;
        buffer += ObjC.unwrap($.NSString.alloc.initWithDataEncoding(data, $.NSUTF8StringEncoding));
        let newline;
        while ((newline = buffer.indexOf("\n")) >= 0) {
            const line = buffer.slice(0, newline);
            buffer = buffer.slice(newline + 1);
            let reply;
            try {
                reply = snapshot(JSON.parse(line));
            } catch (error) {
                musicTab = null;
                reply = JSON.stringify({error: String(error)});
            }
            const bytes = $(String(reply) + "\n").dataUsingEncoding($.NSUTF8StringEncoding);
            output.writeData(bytes);
        }
    }
}
