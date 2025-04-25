#pragma once

// Forward declaration if needed (e.g., for BotController)
class BotController;

namespace GUI {
    // Renders the content of the Bot tab
    // Takes a pointer to BotController to interact with the bot logic
    void render_bot_tab(BotController* botController);
} 