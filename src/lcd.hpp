/*             DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                    Version 2, December 2004

 Copyright (C) 2004 Sam Hocevar <sam@hocevar.net>

 Everyone is permitted to copy and distribute verbatim or modified
 copies of this license document, and changing it is allowed as long
 as the name is changed.

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. You just DO WHAT THE FUCK YOU WANT TO.
*/
#pragma once

using namespace rack;
extern Plugin* pluginInstance;

// This LCD widget  displays data, provided by the module or by widgets.
// Its size is currently fixed to 36*10mm - 2 lines of 11 characters.
//
// The LCD LAYOUT is the layout, e.g., two lines of text, or a piano and a line of text.
// The LCD MODE is deprecated.
//
// Modulus Salomonis Regis is the only module to implement the LCD somewhat as intended.
// Arcane, Darius and QQQQ do things in deprecated ways.
// 
// On Arcane and Darius, the SVG of the LCD is a little bit too small to display
// descenders on the second line of text, so uppercase is mostly used.
// Newer modules have a larger LCD to fit descenders.
//
// It's under heavy rework - I advise against re-using this yet.
// 
// If you're gonna reuse this code despite the warnings, please consider changing my signature
// color scheme to your own. You can recolor the SVG letters in batch with a text editor.

namespace Lcd {


// Which elements to show and hide
// FIXME: Migrate terminology entirely to mode.
enum LcdLayouts {
    // Displays nothing
    OFF_LAYOUT,
    // Displays text on the first line and empties the second
    TEXT1_LAYOUT,
    // Displays text on the second line and empties the first
    TEXT2_LAYOUT,
    // Displays text on two lines
    TEXT1_AND_TEXT2_LAYOUT,
    // Piano on the first line and text on the second
    PIANO_AND_TEXT2_LAYOUT,
};


// Interface between the module & widgets with the LCD
struct LcdStatus {
    // The first line, not displayed on every layout. 
    std::string lcdText1 = "";

    // The second line, currently displayed on every layout (but not every LCD is visually displayed as having two lines)
    std::string lcdText2 = "";

    // The piano display, displayed on the first line only.
    std::array<bool, 12> pianoDisplay;

    // Whether to redraw the widget.
    bool lcdDirty = false;

    // Which mode we're in. Use this to implement custom module logic - the LCD has no clue what this means.
    // FIXME: Deprected, move away from using this.
    int lcdMode = 0;

    // Whether to draw two lines of text, a piano, etc.
    int lcdLayout = OFF_LAYOUT;

    // For any info on a timer in the module. This widget has no knowledge what it means.
    float lcdLastInteraction = 0.f;

    // How long before going back to the main display.
    float notificationTimeout = 3.f;

    LcdStatus() {
        for (int i = 0; i < 12; i++) pianoDisplay[i] = false;
    }

    // Call this from the module.
    // Use this to go back to the main page.
    void notificationStep(float deltaTime) {
        if (lcdLastInteraction >= 0.f) {
            lcdLastInteraction += deltaTime;
        }
        if (lcdLastInteraction >= notificationTimeout) {
            lcdLastInteraction = -1.f;
        }
    }

};

// The draw widget, concerned only with rendering layouts.
template <class TModule>
struct LcdDrawWidget : LightWidget {
    TModule *module;
    std::array<std::shared_ptr<Svg>, 95> asciiSvg; // 32 to 126, the printable range
    std::array<std::shared_ptr<Svg>, 24> pianoSvg; // 0..11: Unlit, 12..23 = Lit
    std::string lcdText1;
    std::string lcdText2;

    LcdDrawWidget(TModule *_module) {
        module = _module;
        if (module) {
            box.size = mm2px(Vec(36.0, 10.0));
            for (int i = 0; i < 12; i++) // Unlit
                pianoSvg[i] = APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/lcd/piano/u" + std::to_string(i) + ".svg"));
            for (int i = 0; i < 12; i++) // Lit
                pianoSvg[i + 12] = APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/lcd/piano/l" + std::to_string(i) + ".svg"));
            for (int i = 0; i < 95; i++)
                asciiSvg[i] = APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/lcd/Fixed_v01/" + std::to_string(i + 32) + ".svg"));
        }
    }

    // Decides what to draw depending on the layout.
    void draw(const DrawArgs &args) override {
        // Avoids crashing the browser
        if (!module) return;

        nvgScale(args.vg, 1.5, 1.5);
    
        // Piano display at the top.
        if ( module->lcdStatus.lcdLayout == PIANO_AND_TEXT2_LAYOUT ) {
            nvgSave(args.vg);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[0])  ? 12 :  0 ]->handle);
            nvgTranslate(args.vg, 6, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[1])  ? 13 :  1 ]->handle);
            nvgTranslate(args.vg, 5, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[2])  ? 14 :  2 ]->handle);
            nvgTranslate(args.vg, 5, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[3])  ? 15 :  3 ]->handle);
            nvgTranslate(args.vg, 5, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[4])  ? 16 :  4 ]->handle);
            nvgTranslate(args.vg, 7, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[5])  ? 17 :  5 ]->handle);
            nvgTranslate(args.vg, 6, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[6])  ? 18 :  6 ]->handle);
            nvgTranslate(args.vg, 5, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[7])  ? 19 :  7 ]->handle);
            nvgTranslate(args.vg, 5, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[8])  ? 20 :  8 ]->handle);
            nvgTranslate(args.vg, 5, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[9])  ? 21 :  9 ]->handle);
            nvgTranslate(args.vg, 5, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[10]) ? 22 : 10 ]->handle);
            nvgTranslate(args.vg, 5, 0);
            svgDraw(args.vg, pianoSvg[(module->lcdStatus.pianoDisplay[11]) ? 23 : 11 ]->handle);
            nvgRestore(args.vg);
        }

        // 11 character display at the top.
        if ( module->lcdStatus.lcdLayout == TEXT1_LAYOUT
            || module->lcdStatus.lcdLayout == TEXT1_AND_TEXT2_LAYOUT ) {
            nvgSave(args.vg);
            lcdText1 = module->lcdStatus.lcdText1;
            lcdText1.append(11, ' '); // Ensure the string is long enough
            for (int i = 0; i < 11; i++) {
                char c = lcdText1.at(i);
                svgDraw(args.vg, asciiSvg[ c - 32 ]->handle);
                nvgTranslate(args.vg, 6, 0);
            }
            nvgRestore(args.vg);
        }
    
        // 11 character display at the bottom.
        if ( module->lcdStatus.lcdLayout == TEXT2_LAYOUT
            || module->lcdStatus.lcdLayout == TEXT1_AND_TEXT2_LAYOUT
            || module->lcdStatus.lcdLayout == PIANO_AND_TEXT2_LAYOUT ) {
            nvgSave(args.vg);
            nvgTranslate(args.vg, 0, 11);
            lcdText2 = module->lcdStatus.lcdText2;
            lcdText2.append(11, ' '); // Ensure the string is long enough
            for (int i = 0; i < 11; i++) {
                char c = lcdText2.at(i);
                svgDraw(args.vg, asciiSvg[ c - 32 ]->handle);
                nvgTranslate(args.vg, 6, 0);
            }
            nvgRestore(args.vg);
        }
    }

}; // LcdDrawWidget


// The framebuffer holding the Draw widget.
template <typename TModule>
struct LcdFramebufferWidget : FramebufferWidget{
    TModule *module;

    LcdFramebufferWidget(TModule *_module){
        module = _module;
    }

    void step() override{
        if (!module) return;
        if(module->lcdStatus.lcdDirty){
            FramebufferWidget::dirty = true;
            module->lcdStatus.lcdDirty = false;
        }
        FramebufferWidget::step();
    }
};

// The actual LCD widget.
template <typename TModule>
struct LcdWidget : TransparentWidget {
    TModule *module;
    Lcd::LcdFramebufferWidget<TModule> *lfb;
    Lcd::LcdDrawWidget<TModule> *ldw;

    LcdWidget(TModule *_module){
        module = _module;
        lfb = new Lcd::LcdFramebufferWidget<TModule>(module);
        ldw = new Lcd::LcdDrawWidget<TModule>(module);
        addChild(lfb);
        lfb->addChild(ldw);
    }

    // Override this to process timeouts and default modes
    void processDefaultMode() {

    }

    void draw(const DrawArgs& args) override {
        processDefaultMode();
        TransparentWidget::draw(args);
    }
};



} // Lcd
