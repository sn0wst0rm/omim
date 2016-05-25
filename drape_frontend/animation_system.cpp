#include "animation_system.hpp"

#include "base/logging.hpp"

#include "std/weak_ptr.hpp"

namespace df
{

namespace
{

class PropertyBlender
{
public:
  PropertyBlender() = default;

  void Blend(Animation::PropertyValue const & value)
  {
    // Now perspective parameters can't be blended.
    if (value.m_type == Animation::PropertyValue::ValuePerspectiveParams)
    {
      m_value = value;
      m_counter = 1;
      return;
    }

    if (m_counter != 0)
    {
      // New value type resets current blended value.
      if (m_value.m_type != value.m_type)
      {
        m_value = value;
        m_counter = 1;
        return;
      }

      if (value.m_type == Animation::PropertyValue::ValueD)
        m_value.m_valueD += value.m_valueD;
      else if (value.m_type == Animation::PropertyValue::ValuePointD)
        m_value.m_valuePointD += value.m_valuePointD;
    }
    else
    {
      m_value = value;
    }
    m_counter++;
  }

  Animation::PropertyValue Finish()
  {
    if (m_counter == 0)
      return m_value;

    double const scalar = 1.0 / m_counter;
    m_counter = 0;
    if (m_value.m_type == Animation::PropertyValue::ValueD)
      m_value.m_valueD *= scalar;
    else if (m_value.m_type == Animation::PropertyValue::ValuePointD)
      m_value.m_valuePointD *= scalar;

    return m_value;
  }

  bool IsEmpty() const
  {
    return m_counter == 0;
  }

private:
  Animation::PropertyValue m_value;
  uint32_t m_counter = 0;
};

} // namespace

bool AnimationSystem::GetRect(ScreenBase const & currentScreen, m2::AnyRectD & rect)
{
  m_lastScreen = currentScreen;

  double scale = currentScreen.GetScale();
  double angle = currentScreen.GetAngle();
  m2::PointD pos = currentScreen.GlobalRect().GlobalZero();

  Animation::PropertyValue value;
  if (GetProperty(Animation::MapPlane, Animation::Scale, value))
    scale = value.m_valueD;

  if (GetProperty(Animation::MapPlane, Animation::Angle, value))
    angle = value.m_valueD;

  if (GetProperty(Animation::MapPlane, Animation::Position, value))
    pos = value.m_valuePointD;

  m2::RectD localRect = currentScreen.PixelRect();
  localRect.Offset(-localRect.Center());
  localRect.Scale(scale);
  rect = m2::AnyRectD(pos, angle, localRect);

  return true;
}

bool AnimationSystem::GetPerspectiveAngle(double & angle)
{
  Animation::PropertyValue value;
  if (GetProperty(Animation::MapPlane, Animation::AnglePerspective, value))
  {
    angle = value.m_valueD;
    return true;
  }
  return false;
}

bool AnimationSystem::SwitchPerspective(Animation::SwitchPerspectiveParams & params)
{
  Animation::PropertyValue value;
  if (GetProperty(Animation::MapPlane, Animation::SwitchPerspective, value))
  {
    params = value.m_valuePerspectiveParams;
    return true;
  }
  return false;
}

bool AnimationSystem::GetArrowPosition(m2::PointD & position)
{
  Animation::PropertyValue value;
  if (GetProperty(Animation::Arrow, Animation::Position, value))
  {
    position = value.m_valuePointD;
    return true;
  }
  return false;
}

bool AnimationSystem::GetArrowAngle(double & angle)
{
  Animation::PropertyValue value;
  if (GetProperty(Animation::Arrow, Animation::Angle, value))
  {
    angle = value.m_valueD;
    return true;
  }
  return false;
}

bool AnimationSystem::AnimationExists(Animation::TObject object) const
{
  if (!m_animationChain.empty())
  {
    for (auto const & anim : *(m_animationChain.front()))
    {
      if (anim->HasObject(object))
        return true;
    }
  }
  for (auto const & prop : m_propertyCache)
  {
    if (prop.first.first == object)
      return true;
  }
  return false;
}

bool AnimationSystem::HasAnimations() const
{
  return !m_animationChain.empty();
}

AnimationSystem & AnimationSystem::Instance()
{
  static AnimationSystem animSystem;
  return animSystem;
}

void AnimationSystem::CombineAnimation(drape_ptr<Animation> animation)
{
  TAnimationList interruptedAnimations;
  bool startImmediately = true;
  for (auto & pList : m_animationChain)
  {
    auto & lst = *pList;
    bool couldBeBlended = animation->CouldBeBlended();

    for (auto it = lst.begin(); it != lst.end();)
    {
      auto & anim = *it;
      if (anim->GetInterruptedOnCombine())
      {
        interruptedAnimations.splice(interruptedAnimations.end(), lst, it++);
      }
      else if (!anim->CouldBeBlendedWith(*animation))
      {
        if (!anim->CouldBeInterrupted())
        {
          couldBeBlended = false;
          break;
        }
        interruptedAnimations.splice(interruptedAnimations.end(), lst, it++);
      }
      else
      {
        ++it;
      }
    }

    for (auto & anim : interruptedAnimations)
    {
      anim->Interrupt();
      SaveAnimationResult(*anim);
    }

    if (couldBeBlended)
    {
      lst.emplace_back(move(animation));
      if (startImmediately)
        lst.back()->OnStart();
      return;
    }
    else if (m_animationChain.size() > 1 && animation->CouldBeInterrupted())
    {
      return;
    }

    startImmediately = false;
  }
  
  PushAnimation(move(animation));
}

void AnimationSystem::PushAnimation(drape_ptr<Animation> animation)
{
  shared_ptr<TAnimationList> pList(new TAnimationList());
  pList->emplace_back(move(animation));

  bool startImmediately = m_animationChain.empty();
  m_animationChain.push_back(pList);
  if (startImmediately)
    pList->front()->OnStart();
}

void AnimationSystem::FinishAnimations(function<bool(shared_ptr<Animation> const &)> const & predicate,
                                       bool rewind, bool finishAll)
{
  if (m_animationChain.empty())
    return;

  TAnimationList & frontList = *(m_animationChain.front());
  TAnimationList finishAnimations;
  for (auto it = frontList.begin(); it != frontList.end();)
  {
    auto & anim = *it;
    if (predicate(anim))
      finishAnimations.splice(finishAnimations.end(), frontList, it++);
    else
      ++it;
  }

  if (finishAll)
  {
    for (auto & pList : m_animationChain)
    {
      auto & lst = *pList;
      for (auto it = lst.begin(); it != lst.end();)
      {
        if (predicate(*it))
          it = lst.erase(it);
        else
          ++it;
      }
    }
  }

  for (auto & anim : finishAnimations)
  {
    if (rewind)
      anim->Finish();
    SaveAnimationResult(*anim);
  }

  if (frontList.empty())
    StartNextAnimations();
}

void AnimationSystem::FinishAnimations(Animation::Type type, bool rewind, bool finishAll)
{
  FinishAnimations([&type](shared_ptr<Animation> const & anim) { return anim->GetType() == type; },
                   rewind, finishAll);
}

void AnimationSystem::FinishObjectAnimations(Animation::TObject object, bool rewind, bool finishAll)
{
  FinishAnimations([&object](shared_ptr<Animation> const & anim) { return anim->HasObject(object); },
                   rewind, finishAll);
}

void AnimationSystem::Advance(double elapsedSeconds)
{
  if (m_animationChain.empty())
    return;

  TAnimationList finishedAnimations;
  TAnimationList & frontList = *(m_animationChain.front());
  for (auto it = frontList.begin(); it != frontList.end();)
  {
    auto & anim = *it;
    anim->Advance(elapsedSeconds);
    if (anim->IsFinished())
    {
      finishedAnimations.splice(finishedAnimations.end(), frontList, it++);
    }
    else
    {
      ++it;
    }
  }
  for (auto & anim : finishedAnimations)
  {
    anim->OnFinish();
    SaveAnimationResult(*anim);
  }
  if (frontList.empty())
    StartNextAnimations();
}

bool AnimationSystem::GetProperty(Animation::TObject object, Animation::TProperty property,
                                  Animation::PropertyValue & value) const
{
  if (!m_animationChain.empty())
  {
    PropertyBlender blender;
    for (auto const & anim : *(m_animationChain.front()))
    {
      if (anim->HasProperty(object, property))
      {
        Animation::PropertyValue val;
        if (anim->GetProperty(object, property, val))
          blender.Blend(val);
      }
    }
    if (!blender.IsEmpty())
    {
      value = blender.Finish();
      return true;
    }
  }

  auto it = m_propertyCache.find(make_pair(object, property));
  if (it != m_propertyCache.end())
  {
    value = it->second;
    m_propertyCache.erase(it);
    return true;
  }
  return false;
}

void AnimationSystem::SaveAnimationResult(Animation const & animation)
{
  for (auto const & object : animation.GetObjects())
  {
    for (auto const & property : animation.GetProperties(object))
    {
      Animation::PropertyValue value;
      if (animation.GetProperty(object, property, value))
        m_propertyCache[make_pair(object, property)] = value;
    }
  }
}

void AnimationSystem::StartNextAnimations()
{
  if (m_animationChain.empty())
    return;

  m_animationChain.pop_front();
  if (!m_animationChain.empty())
  {
    vector<weak_ptr<Animation>> startedAnimations;
    startedAnimations.reserve(m_animationChain.front()->size());
    for (auto & anim : *(m_animationChain.front()))
      startedAnimations.push_back(anim);

    //TODO (in future): use propertyCache to load start values to the next animations
    for (auto & weak_anim : startedAnimations)
    {
      shared_ptr<Animation> anim = weak_anim.lock();
      if (anim != nullptr)
        anim->OnStart();
    }
  }
}

} // namespace df