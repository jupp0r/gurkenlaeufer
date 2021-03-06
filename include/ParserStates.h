#pragma once
#include "ParserInterface.h"
#include "TestcaseInterface.h"

// from http://stackoverflow.com/a/236803
inline void split(const std::string& s, char delim, std::vector<std::string>& elems)
{
    std::stringstream ss;
    ss.str(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
}

inline std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

inline std::string toLower(const std::string& input)
{
    std::string output(input);
    std::transform(input.begin(), input.end(), output.begin(), ::tolower);
    return output;
}

inline bool isStepKeyword(std::string word)
{
    return word == "given" || word == "when" || word == "then" || word == "and";
}

auto isSpace = [](int c) { return std::isspace(c); };

class ExamplesState : public ParserState {
public:
    ExamplesState(IParserStateFactory& factory, TestSteps steps, TestSteps backgroudSteps, const ITestcaseCollectionSPtr& testcases)
        : _factory(factory)
        , _Steps(std::move(steps))
        , _backgroudSteps(std::move(backgroudSteps))
        , _testcases(testcases)
        , _isTableHead(true)
    {
    }

    std::unique_ptr<ParserState> parseLine(const std::string& trimmedLine) override
    {
        if (trimmedLine[0] != '|') {
            // forward to next state
            return _factory.createInitialState(std::move(_backgroudSteps))->parseLine(trimmedLine);
        }

        std::string lineWithoutSpaces(trimmedLine);
        lineWithoutSpaces.erase(std::remove(lineWithoutSpaces.begin(), lineWithoutSpaces.end(), ' '), lineWithoutSpaces.end());

        if (_isTableHead) {
            _isTableHead = false;
            _TableHead = split(lineWithoutSpaces, '|');
            return nullptr;
        }

        auto entries = split(lineWithoutSpaces, '|');
        _generateSteps(entries);
        return nullptr;
    }

private:
    void _generateSteps(const std::vector<std::string>& params)
    {
        TestSteps steps(_Steps.size());
        std::transform(_Steps.begin(), _Steps.end(), steps.begin(), [&](const std::string& step) {
            return _replaceParamsInStep(step, params);
        });
        _testcases->appendTest(std::move(steps));
    }

    std::string _replaceParamsInStep(const std::string& step, const std::vector<std::string>& params) const
    {
        auto isLessThanSign = [](int c) { return c == '<'; };
        auto isGreaterThanSign = [](int c) { return c == '>'; };

        auto outStep = step;
        for (;;) {
            const auto start = std::find_if(outStep.begin(), outStep.end(), isLessThanSign);
            if (start == outStep.end()) {
                return outStep;
            }
            const auto end = std::find_if(start, outStep.end(), isGreaterThanSign);
            if (end == outStep.end()) {
                throw std::runtime_error("found '<' but no matching '>'");
            }
            const auto variableName = std::string(start + 1, end);

            const auto indexOfVariable = std::find(_TableHead.begin(), _TableHead.end(), variableName) - _TableHead.begin();

            outStep = std::string(outStep.begin(), start) + params[indexOfVariable] + std::string(end + 1, outStep.end());
        }
    }

    IParserStateFactory& _factory;
    TestSteps _Steps;
    TestSteps _backgroudSteps;
    ITestcaseCollectionSPtr _testcases;
    std::vector<std::string> _TableHead;
    bool _isTableHead;
};

class ScenarioOutlineState : public ParserState {
public:
    ScenarioOutlineState(IParserStateFactory& factory, TestSteps backgroudSteps)
        : _factory(factory)
        , _backgroudSteps(std::move(backgroudSteps))
    {
        _Steps = _backgroudSteps;
    }

    std::unique_ptr<ParserState> parseLine(const std::string& trimmedLine) override
    {
        auto firstWordEnd = std::find_if(trimmedLine.begin(), trimmedLine.end(), isSpace);

        auto firstWord = toLower(std::string(trimmedLine.begin(), firstWordEnd));
        if (isStepKeyword(firstWord)) {
            auto secondWordBegin = firstWordEnd + 1;
            _Steps.emplace_back(std::string(secondWordBegin, trimmedLine.end()));
        } else {
            return _factory.createExamplesState(std::move(_Steps), std::move(_backgroudSteps));
        }
        return nullptr;
    }

private:
    IParserStateFactory& _factory;
    TestSteps _backgroudSteps;
    std::list<std::string> _Steps;
};

class ScenarioState : public ParserState {
public:
    ScenarioState(IParserStateFactory& factory, TestSteps backgroudSteps, const ITestcaseCollectionSPtr& testcases)
        : _factory(factory)
        , _backgroudSteps(std::move(backgroudSteps))
        , _testcases(testcases)
    {
        _Steps = _backgroudSteps;
    }

    std::unique_ptr<ParserState> parseLine(const std::string& trimmedLine) override
    {
        auto firstWordEnd = std::find_if(trimmedLine.begin(), trimmedLine.end(), isSpace);

        auto firstWord = toLower(std::string(trimmedLine.begin(), firstWordEnd));
        if (isStepKeyword(firstWord)) {
            auto secondWordBegin = firstWordEnd + 1;
            _Steps.emplace_back(std::string(secondWordBegin, trimmedLine.end()));
        } else {
            _testcases->appendTest(std::move(_Steps));
            // forward to next state
            return _factory.createInitialState(std::move(_backgroudSteps))->parseLine(trimmedLine);
        }
        return nullptr;
    }

private:
    IParserStateFactory& _factory;
    TestSteps _backgroudSteps;
    ITestcaseCollectionSPtr _testcases;
    TestSteps _Steps;
};

class BackgroundState : public ParserState {
public:
    BackgroundState(IParserStateFactory& factory)
        : _factory(factory)
    {
    }

    std::unique_ptr<ParserState> parseLine(const std::string& trimmedLine) override
    {
        auto firstWordEnd = std::find_if(trimmedLine.begin(), trimmedLine.end(), isSpace);

        auto firstWord = toLower(std::string(trimmedLine.begin(), firstWordEnd));
        if (isStepKeyword(firstWord)) {
            auto secondWordBegin = firstWordEnd + 1;
            _Steps.emplace_back(std::string(secondWordBegin, trimmedLine.end()));
        } else {
            // forward to next state
            return _factory.createInitialState(std::move(_Steps))->parseLine(trimmedLine);
        }
        return nullptr;
    }

private:
    IParserStateFactory& _factory;
    std::list<std::string> _Steps;
};

class InitialState : public ParserState {
public:
    InitialState(IParserStateFactory& factory, TestSteps backgroudSteps)
        : _factory(factory)
        , _backgroudSteps(std::move(backgroudSteps))
    {
    }

    std::unique_ptr<ParserState> parseLine(const std::string& trimmedLine) override
    {
        auto isSpace = [](int c) { return std::isspace(c); };
        auto fistWordEnd = std::find_if(trimmedLine.begin(), trimmedLine.end(), isSpace);
        const std::string firstWord(trimmedLine.begin(), fistWordEnd);
        if (firstWord == "Scenario") {
            auto secondWordBegin = fistWordEnd + 1;
            auto secondWordEnd = std::find_if(secondWordBegin, trimmedLine.end(), isSpace);
            const std::string secondWord(secondWordBegin, secondWordEnd);
            if (secondWord == "Outline:") {
                return _factory.createScenarioOutlineState(std::move(_backgroudSteps));
            }
        } else if (firstWord == "Scenario:") {
            return _factory.createScenarioState(std::move(_backgroudSteps));
        } else if (firstWord == "Background:") {
            return _factory.createBackgroundState();
        }
        return nullptr;
    }

private:
    IParserStateFactory& _factory;
    TestSteps _backgroudSteps;
};
