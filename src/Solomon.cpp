/*  Copyright (C) 2019-2020 Aria Salvatrice
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

// Self-modifying sequencer. Internally, the slots are called "nodes", "step" refers to the movement.
// For now, only a 8-node version. If there is interest, other versions can be made later.

// TODO: Clear queue each step or not: toggle
// TODO: Portable sequences!

#include "plugin.hpp"
#include "lcd.hpp"
#include "quantizer.hpp"

namespace Solomon {

const float READWINDOWDURATION = 1.f; // Seconds

enum StepTypes {
    STEP_QUEUE,
    STEP_TELEPORT,
    STEP_WALK,
    STEP_BACK,
    STEP_FORWARD
};

enum LcdModes {
    INIT_MODE,
    SCALE_MODE,
    MINMAX_MODE,
    SLIDE_MODE
};

template <size_t NODES>
struct Solomon : Module {
    enum ParamIds {
        KEY_PARAM,
        SCALE_PARAM,
        MIN_PARAM,
        MAX_PARAM,
        SLIDE_PARAM,
        CLEAR_ON_STEP_PARAM,
        ENUMS(NODE_SUB_1_SD_PARAM, NODES),
        ENUMS(NODE_ADD_1_SD_PARAM, NODES),
        ENUMS(NODE_QUEUE_PARAM, NODES),
        NUM_PARAMS
    };
    enum InputIds {
        EXT_SCALE_INPUT,
        STEP_QUEUE_INPUT,
        STEP_TELEPORT_INPUT,
        STEP_WALK_INPUT,
        STEP_BACK_INPUT,
        STEP_FORWARD_INPUT,
        ENUMS(NODE_SUB_1_SD_INPUT, NODES),
        ENUMS(NODE_SUB_2_SD_INPUT, NODES),
        ENUMS(NODE_SUB_3_SD_INPUT, NODES),
        ENUMS(NODE_SUB_1_OCT_INPUT, NODES),
        ENUMS(NODE_ADD_1_SD_INPUT, NODES),
        ENUMS(NODE_ADD_2_SD_INPUT, NODES),
        ENUMS(NODE_ADD_3_SD_INPUT, NODES),
        ENUMS(NODE_ADD_1_OCT_INPUT, NODES),
        ENUMS(NODE_QUEUE_INPUT, NODES),
        NUM_INPUTS
    };
    enum OutputIds {
        GATE_OUTPUT,
        CV_OUTPUT,
        ENUMS(REACHED_OUTPUT, NODES),
        ENUMS(CHANCE_OUTPUT, NODES),
        ENUMS(LATCH_OUTPUT, NODES),
        ENUMS(NEXT_OUTPUT, NODES),
        ENUMS(NODE_CV_OUTPUT, NODES),
        NUM_OUTPUTS
    };
    enum LightIds {
        ENUMS(NODE_LIGHT, NODES),
        NUM_LIGHTS
    };

    // Global
    int stepType = -1;
    float readWindow = -1.f; // -1 when closed
    int currentNode = 0;
    std::array<bool, 12> scale;
    dsp::SchmittTrigger stepQueueTrigger;
    dsp::SchmittTrigger stepTeleportTrigger;
    dsp::SchmittTrigger stepWalkTrigger;
    dsp::SchmittTrigger stepBackTrigger;
    dsp::SchmittTrigger stepForwardTrigger;
    Lcd::LcdStatus lcdStatus;

    // Per node
    float cv[NODES];
    std::array<bool, NODES> queue;
    std::array<bool, NODES> next;
    dsp::SchmittTrigger sub1SdTrigger[NODES];
    dsp::SchmittTrigger add1SdTrigger[NODES];

    Solomon() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(MIN_PARAM, 0.f, 10.f, 3.f, "Minimum Note");
        configParam(MAX_PARAM, 0.f, 10.f, 5.f, "Maximum Note");
        configParam(SLIDE_PARAM, 0.f, 10.f, 0.f, "Slide");

        // C Minor is the default
        configParam(KEY_PARAM, 0.f, 11.f, 0.f, "Key");
        configParam(SCALE_PARAM, 0.f, (float) Quantizer::NUM_SCALES - 1, 2.f, "Scale");
        scale = Quantizer::validNotesInScaleKey(Quantizer::NATURAL_MINOR, 0);

        clearQueue();
        clearNext();
        // Default note is 0V - C4
        for(size_t i = 0; i < NODES; i++) cv[i] = 0.f;

        lcdStatus.lcdPage = Lcd::TEXT1_PAGE;
        lcdStatus.lcdMode = INIT_MODE;
        lcdStatus.lcdText1 = "Summoning..";
    }

    // How many nodes are enqueued
    size_t queueCount() {
        size_t count = 0;
        for(size_t i = 0; i < NODES; i++) {
            if (queue[i] == true) count++;
        }
        DEBUG("QUEUE: %d", count);
        return count;
    }

    float getMinCv() {
        if(params[MIN_PARAM].getValue() <= params[MAX_PARAM].getValue()) {
            return params[MIN_PARAM].getValue() - 4.f;
        } else {
            return params[MAX_PARAM].getValue() - 4.f;
        }
    }

    float getMaxCv() {
        if(params[MIN_PARAM].getValue() <= params[MAX_PARAM].getValue()) {
            return params[MAX_PARAM].getValue() - 4.f;
        } else {
            return params[MIN_PARAM].getValue() - 4.f;
        }
    }

    // Subtracts scale degrees. Wraps around on overflow.
    void subSd(size_t node, size_t sd) {
        for (size_t i = 0; i < sd; i++) {
            cv[node] = Quantizer::quantize(cv[node], scale, - 1);
            if(cv[node] < getMinCv()) {
                cv[node] = Quantizer::quantize(getMaxCv(), scale);
            }
        }
    }

    // Adds scale degrees. Wraps around on overflow.
    void addSd(size_t node, size_t sd) {
        for (size_t i = 0; i < sd; i++) {
            cv[node] = Quantizer::quantize(cv[node], scale, 1);
            if(cv[node] > getMaxCv()) {
                cv[node] = Quantizer::quantize(getMinCv(), scale);
            }
        }
    }

    // Each node has 2 manual - and + buttons that are processed whether in a window or not.
    void processSdButtons() {
        for (size_t i = 0; i < NODES; i++) {
            if(sub1SdTrigger[i].process(params[NODE_SUB_1_SD_PARAM + i].getValue())) {
                subSd(i, 1);
            }
            if(add1SdTrigger[i].process(params[NODE_ADD_1_SD_PARAM + i].getValue())) {
                addSd(i, 1);
            }
        }
    }

    // Opens a window if a step input is reached, and remembers what type it is.
    // If it's a queue input, something must be enqueued. 
    int readStepInputs() {
        if (stepQueueTrigger.process(inputs[STEP_QUEUE_INPUT].getVoltageSum()) && queueCount() > 0) return STEP_QUEUE;
        if (stepTeleportTrigger.process(inputs[STEP_TELEPORT_INPUT].getVoltageSum()))               return STEP_TELEPORT;
        if (stepWalkTrigger.process(inputs[STEP_WALK_INPUT].getVoltageSum()))                       return STEP_WALK;
        if (stepBackTrigger.process(inputs[STEP_BACK_INPUT].getVoltageSum()))                       return STEP_BACK;
        if (stepForwardTrigger.process(inputs[STEP_FORWARD_INPUT].getVoltageSum()))                 return STEP_FORWARD;
        return -1;
    }

    void clearQueue() {
        for(size_t i = 0; i < NODES; i++) queue[i] = false;
    }

    void clearNext() {
        for(size_t i = 0; i < NODES; i++) next[i] = false;
    }

    // During Read Windows, see if we received queue messages.
    void updateQueue() {
        for(size_t i = 0; i < NODES; i++) {
            if (inputs[NODE_QUEUE_INPUT + i].getVoltageSum() > 0.f) {
                queue[i] = true;
            }
        }
    }

    void processReadWindow() {
        updateQueue();
    }

    // A read window just elapsed, we move to the next step and send the outputs
    void processStep() {

    }

    void process(const ProcessArgs& args) override {
        processSdButtons();

        if (readWindow < 0.f) {
            // We are not in a Read Window
            stepType = readStepInputs();
            if (stepType >= 0) readWindow = 0.f;
        }
        if (readWindow >= 0.f && readWindow < READWINDOWDURATION) {
            // We are in a Read Window
            processReadWindow();
            readWindow += args.sampleTime;
        }
        if (readWindow >= READWINDOWDURATION) {
            // A read window closed
            processStep();
            readWindow = -1.f;
        }
    }


};




///////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////




// Scale/key knobs
template <typename TModule>
struct ScaleKnob : AriaKnob820 {
    ScaleKnob() {
        snap = true;
        AriaKnob820();
    }
    void onDragMove(const event::DragMove& e) override {
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdLastInteraction = 0.f;
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdDirty = true;
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdMode = SCALE_MODE;
        AriaKnob820::onDragMove(e);
    }
};

// Min/Max knobs
template <typename TModule>
struct MinMaxKnob : AriaKnob820 {
    void onDragMove(const event::DragMove& e) override {
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdLastInteraction = 0.f;
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdDirty = true;
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdMode = MINMAX_MODE;
        AriaKnob820::onDragMove(e);
    }
};

// Slide knobs
template <typename TModule>
struct SlideKnob : AriaKnob820 {
    void onDragMove(const event::DragMove& e) override {
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdLastInteraction = 0.f;
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdDirty = true;
        dynamic_cast<TModule*>(paramQuantity->module)->lcdStatus.lcdMode = SLIDE_MODE;
        AriaKnob820::onDragMove(e);
    }
};

// Per-node segment display
// FIXME: Adding a framebuffer doesn't seem to work for fonts.
template <typename TModule>
struct SegmentDisplay : TransparentWidget {
	TModule* module;
    size_t node;
	std::shared_ptr<Font> font;
    std::string text = "*!*";
    float lastCv = -20.f;

	SegmentDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/dseg/DSEG14ClassicMini-Italic.ttf"));
	}

	void draw(const DrawArgs& args) override {
		nvgFontSize(args.vg, 20);
		nvgFontFaceId(args.vg, font->handle);
		nvgTextLetterSpacing(args.vg, 2.0);
		nvgFillColor(args.vg, nvgRGB(0x0b, 0x57, 0x63));
		nvgText(args.vg, 0, 0, "~~~", NULL);
		nvgFillColor(args.vg, nvgRGB(0xc1, 0xf0, 0xf2));
        if(module) {
            if (module->cv[node] != lastCv) {
                text = Quantizer::noteOctaveSegmentName(module->cv[node]);
            }
            lastCv = module->cv[node];
            nvgText(args.vg, 0, 0, text.c_str(), NULL);
        }
	}
};


// The QUEUE message on the segment display
template <typename TModule>
struct QueueWidget : Widget {
	TModule* module;
    size_t node;
	FramebufferWidget* framebuffer;
	SvgWidget* svgWidget;
    bool lastStatus; // Start on the wrong one to force a refresh

    QueueWidget() {
        framebuffer = new widget::FramebufferWidget;
        addChild(framebuffer);
        svgWidget = new widget::SvgWidget;
        svgWidget->setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/solomon-queue-lit.svg")));
        framebuffer->box.size = svgWidget->box.size;
        box.size = svgWidget->box.size;
        framebuffer->addChild(svgWidget);
        lastStatus = true;
    }

    void step() override {
        if(module) {
            if (module->queue[node] != lastStatus) {
                framebuffer->visible = (module->queue[node] == true) ? true : false;
            }
            lastStatus = module->queue[node];
        }
        Widget::step();
    }
};

// The NEXT message on the segment display
template <typename TModule>
struct NextWidget : Widget {
	TModule* module;
    size_t node;
	FramebufferWidget* framebuffer;
	SvgWidget* svgWidget;
    bool lastStatus; // Start on the wrong one to force a refresh

    NextWidget() {
        framebuffer = new widget::FramebufferWidget;
        addChild(framebuffer);
        svgWidget = new widget::SvgWidget;
        svgWidget->setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/solomon-next-lit.svg")));
        framebuffer->box.size = svgWidget->box.size;
        box.size = svgWidget->box.size;
        framebuffer->addChild(svgWidget);
        lastStatus = true;
    }

    void step() override {
        if(module) {
            if (module->queue[node] != lastStatus) {
                framebuffer->visible = (module->next[node] == true) ? true : false;
            }
            lastStatus = module->next[node];
        }
        Widget::step();
    }
};


struct SolomonWidget8 : ModuleWidget {

    SolomonWidget8(Solomon<8>* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/faceplates/Solomon.svg")));
        
        // Screws
        addChild(createWidget<AriaScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<AriaScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<AriaScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<AriaScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Signature
        addChild(createWidget<AriaSignature>(mm2px(Vec(38.0, 114.5))));

        // Global step inputs. Ordered counterclockwise.
        addInput(createInput<AriaJackIn>(mm2px(Vec(20.f, 17.f)), module, Solomon<8>::STEP_QUEUE_INPUT));
        addInput(createInput<AriaJackIn>(mm2px(Vec( 5.f, 32.f)), module, Solomon<8>::STEP_TELEPORT_INPUT));
        addInput(createInput<AriaJackIn>(mm2px(Vec(35.f, 32.f)), module, Solomon<8>::STEP_FORWARD_INPUT));
        addInput(createInput<AriaJackIn>(mm2px(Vec(10.f, 47.f)), module, Solomon<8>::STEP_WALK_INPUT));
        addInput(createInput<AriaJackIn>(mm2px(Vec(30.f, 47.f)), module, Solomon<8>::STEP_BACK_INPUT));

        // LCD
        addChild(Lcd::createLcd<Solomon<8>>(mm2px(Vec(7.7f, 65.3f)), module));

        addParam(createParam<ScaleKnob<Solomon<8>>>(mm2px(Vec(8.f, 74.f)), module, Solomon<8>::SCALE_PARAM));
        addParam(createParam<ScaleKnob<Solomon<8>>>(mm2px(Vec(20.f, 74.f)), module, Solomon<8>::KEY_PARAM));
        addInput(createInput<AriaJackIn>(mm2px(Vec(32.f, 74.f)), module, Solomon<8>::EXT_SCALE_INPUT));

        addParam(createParam<MinMaxKnob<Solomon<8>>>(mm2px(Vec(8.f, 84.f)), module, Solomon<8>::MIN_PARAM));
        addParam(createParam<MinMaxKnob<Solomon<8>>>(mm2px(Vec(20.f, 84.f)), module, Solomon<8>::MAX_PARAM));
        addParam(createParam<SlideKnob<Solomon<8>>>(mm2px(Vec(32.f, 84.f)), module, Solomon<8>::SLIDE_PARAM));

        // Global output
        addOutput(createOutput<AriaJackOut>(mm2px(Vec(15.f, 110.f)), module, Solomon<8>::GATE_OUTPUT));
        addOutput(createOutput<AriaJackOut>(mm2px(Vec(25.f, 110.f)), module, Solomon<8>::CV_OUTPUT));

        // Nodes
        float xOffset = 53.f;
        float yOffset = 17.f;
        for(size_t i = 0; i < 8; i++) {
            // Inputs
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset +  5.f, yOffset +  0.f)), module, Solomon<8>::NODE_QUEUE_INPUT     + i));
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset +  0.f, yOffset + 10.f)), module, Solomon<8>::NODE_SUB_1_OCT_INPUT + i));
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset +  0.f, yOffset + 20.f)), module, Solomon<8>::NODE_SUB_3_SD_INPUT  + i));
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset +  0.f, yOffset + 30.f)), module, Solomon<8>::NODE_SUB_2_SD_INPUT  + i));
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset +  0.f, yOffset + 40.f)), module, Solomon<8>::NODE_SUB_1_SD_INPUT  + i));
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset + 10.f, yOffset + 10.f)), module, Solomon<8>::NODE_ADD_1_OCT_INPUT + i));
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset + 10.f, yOffset + 20.f)), module, Solomon<8>::NODE_ADD_3_SD_INPUT  + i));
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset + 10.f, yOffset + 30.f)), module, Solomon<8>::NODE_ADD_2_SD_INPUT  + i));
            addInput(createInput<AriaJackIn>(mm2px(Vec(xOffset + 10.f, yOffset + 40.f)), module, Solomon<8>::NODE_ADD_1_SD_INPUT  + i));

            // Segment Display
            SegmentDisplay<Solomon<8>>* display = new SegmentDisplay<Solomon<8>>();
            display->module = module;
            display->node = i;
            display->box.size = mm2px(Vec(20.f, 10.f));
            display->box.pos = mm2px(Vec(xOffset + 0.f, yOffset + 58.f));
            addChild(display);
            QueueWidget<Solomon<8>>* queueWidget = new QueueWidget<Solomon<8>>;
            queueWidget->box.pos = mm2px(Vec(xOffset + 0.25f, yOffset + 59.0f));
            queueWidget->module = module;
            queueWidget->node = i;
            addChild(queueWidget);
            NextWidget<Solomon<8>>* nextWidget = new NextWidget<Solomon<8>>;
            nextWidget->box.pos = mm2px(Vec(xOffset + 9.85f, yOffset + 59.0f));
            nextWidget->module = module;
            nextWidget->node = i;
            addChild(nextWidget);

            // Buttons
            addParam(createParam<AriaPushButton820Momentary>(mm2px(Vec(xOffset +  0.f, yOffset + 64.f)), module, Solomon<8>::NODE_SUB_1_SD_PARAM + i));
            addParam(createParam<AriaPushButton820Momentary>(mm2px(Vec(xOffset + 10.f, yOffset + 64.f)), module, Solomon<8>::NODE_ADD_1_SD_PARAM + i));
            addParam(createParam<AriaPushButton820Momentary>(mm2px(Vec(xOffset +  5.f, yOffset + 71.f)), module, Solomon<8>::NODE_QUEUE_PARAM + i));

            // Outputs
            addOutput(createOutput<AriaJackOut>(mm2px(Vec(xOffset + 10.f, yOffset +  80.f)), module, Solomon<8>::REACHED_OUTPUT + i));
            addOutput(createOutput<AriaJackOut>(mm2px(Vec(xOffset +  0.f, yOffset +  85.f)), module, Solomon<8>::CHANCE_OUTPUT  + i));
            addOutput(createOutput<AriaJackOut>(mm2px(Vec(xOffset + 10.f, yOffset +  90.f)), module, Solomon<8>::LATCH_OUTPUT   + i));
            addOutput(createOutput<AriaJackOut>(mm2px(Vec(xOffset +  0.f, yOffset +  95.f)), module, Solomon<8>::NEXT_OUTPUT    + i));
            addOutput(createOutput<AriaJackOut>(mm2px(Vec(xOffset + 10.f, yOffset + 100.f)), module, Solomon<8>::NODE_CV_OUTPUT + i));

            xOffset += 25.f;
        }
    }
};

} // Namespace Solomon

Model* modelSolomon = createModel<Solomon::Solomon<8>, Solomon::SolomonWidget8>("Solomon");
