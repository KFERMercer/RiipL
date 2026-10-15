#include "TranslationActivity.h"

#include "core/translation/TranslationPool.h"

TranslationActivity::TranslationActivity(QObject* parent)
    : QObject(parent)
{
    TranslationPool* pool = TranslationPool::instance();
    connect(pool, &TranslationPool::busyChanged, this, &TranslationActivity::onBusyChanged);
    connect(pool, &TranslationPool::requestFailed, this, &TranslationActivity::onRequestFailed);
}

void TranslationActivity::markReady()
{
    setState(TranslationPool::instance()->isBusy() ? State::Busy : State::Ready);
}

void TranslationActivity::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void TranslationActivity::onBusyChanged(bool busy)
{
    if (busy) {
        setState(State::Busy);
        return;
    }
    // A failure outlives the queue that carried it, so only a run that was under
    // way ends as done.
    if (m_state == State::Busy)
        setState(State::Done);
}

void TranslationActivity::onRequestFailed()
{
    setState(State::Failure);
}
