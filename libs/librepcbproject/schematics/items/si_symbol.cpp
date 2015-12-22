/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 Urban Bruhin
 * http://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*****************************************************************************************
 *  Includes
 ****************************************************************************************/

#include <QtCore>
#include "si_symbol.h"
#include "si_symbolpin.h"
#include "../schematic.h"
#include "../../project.h"
#include "../../circuit/circuit.h"
#include "../../library/projectlibrary.h"
#include "../../circuit/componentinstance.h"
#include <librepcblibrary/cmp/component.h>
#include <librepcblibrary/sym/symbol.h>
#include <librepcbcommon/fileio/xmldomelement.h>
#include <librepcbcommon/graphics/graphicsscene.h>

namespace project {

/*****************************************************************************************
 *  Constructors / Destructor
 ****************************************************************************************/

SI_Symbol::SI_Symbol(Schematic& schematic, const XmlDomElement& domElement) throw (Exception) :
    SI_Base(), mSchematic(schematic), mComponentInstance(nullptr),
    mSymbVarItem(nullptr), mSymbol(nullptr), mGraphicsItem(nullptr)
{
    mUuid = domElement.getAttribute<Uuid>("uuid");

    Uuid gcUuid = domElement.getAttribute<Uuid>("component_instance");
    mComponentInstance = schematic.getProject().getCircuit().getComponentInstanceByUuid(gcUuid);
    if (!mComponentInstance)
    {
        throw RuntimeError(__FILE__, __LINE__, gcUuid.toStr(),
            QString(tr("No component with the UUID \"%1\" found in the circuit!"))
            .arg(gcUuid.toStr()));
    }

    mPosition.setX(domElement.getFirstChild("position", true)->getAttribute<Length>("x"));
    mPosition.setY(domElement.getFirstChild("position", true)->getAttribute<Length>("y"));
    mRotation = domElement.getFirstChild("position", true)->getAttribute<Angle>("rotation");

    Uuid symbVarItemUuid = domElement.getAttribute<Uuid>("symbol_item");
    init(symbVarItemUuid);
}

SI_Symbol::SI_Symbol(Schematic& schematic, ComponentInstance& cmpInstance, const Uuid& symbolItem,
                     const Point& position, const Angle& rotation) throw (Exception) :
    SI_Base(), mSchematic(schematic), mComponentInstance(&cmpInstance),
    mSymbVarItem(nullptr), mSymbol(nullptr), mGraphicsItem(nullptr), mPosition(position),
    mRotation(rotation)
{
    mUuid = Uuid::createRandom(); // generate random UUID
    init(symbolItem);
}

void SI_Symbol::init(const Uuid& symbVarItemUuid) throw (Exception)
{
    mSymbVarItem = mComponentInstance->getSymbolVariant().getItemByUuid(symbVarItemUuid);
    if (!mSymbVarItem)
    {
        throw RuntimeError(__FILE__, __LINE__, symbVarItemUuid.toStr(),
            QString(tr("The symbol variant item UUID \"%1\" is invalid."))
            .arg(symbVarItemUuid.toStr()));
    }

    mSymbol = mSchematic.getProject().getLibrary().getSymbol(mSymbVarItem->getSymbolUuid());
    if (!mSymbol)
    {
        throw RuntimeError(__FILE__, __LINE__, mSymbVarItem->getSymbolUuid().toStr(),
            QString(tr("No symbol with the UUID \"%1\" found in the project's library."))
            .arg(mSymbVarItem->getSymbolUuid().toStr()));
    }

    mGraphicsItem = new SGI_Symbol(*this);
    mGraphicsItem->setPos(mPosition.toPxQPointF());
    mGraphicsItem->setRotation(-mRotation.toDeg());

    foreach (const library::SymbolPin* libPin, mSymbol->getPins())
    {
        SI_SymbolPin* pin = new SI_SymbolPin(*this, libPin->getUuid());
        if (mPins.contains(libPin->getUuid()))
        {
            throw RuntimeError(__FILE__, __LINE__, libPin->getUuid().toStr(),
                QString(tr("The symbol pin UUID \"%1\" is defined multiple times."))
                .arg(libPin->getUuid().toStr()));
        }
        if (!mSymbVarItem->getPinSignalMap().contains(libPin->getUuid()))
        {
            throw RuntimeError(__FILE__, __LINE__, libPin->getUuid().toStr(),
                QString(tr("Symbol pin UUID \"%1\" not found in pin-signal-map."))
                .arg(libPin->getUuid().toStr()));
        }
        mPins.insert(libPin->getUuid(), pin);
    }
    if (mPins.count() != mSymbVarItem->getPinSignalMap().count())
    {
        throw RuntimeError(__FILE__, __LINE__,
            QString("%1!=%2").arg(mPins.count()).arg(mSymbVarItem->getPinSignalMap().count()),
            QString(tr("The pin count of the symbol instance \"%1\" does not match with "
            "the pin-signal-map")).arg(mUuid.toStr()));
    }

    // connect to the "attributes changes" signal of schematic and component instance
    connect(mComponentInstance, &ComponentInstance::attributesChanged,
            this, &SI_Symbol::schematicOrComponentAttributesChanged);

    if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);
}

SI_Symbol::~SI_Symbol() noexcept
{
    qDeleteAll(mPins);              mPins.clear();
    delete mGraphicsItem;           mGraphicsItem = 0;
}

/*****************************************************************************************
 *  Getters
 ****************************************************************************************/

Project& SI_Symbol::getProject() const noexcept
{
    return mSchematic.getProject();
}

QString SI_Symbol::getName() const noexcept
{
    return mComponentInstance->getName() % mSymbVarItem->getSuffix();
}

/*****************************************************************************************
 *  Setters
 ****************************************************************************************/

void SI_Symbol::setPosition(const Point& newPos) throw (Exception)
{
    mPosition = newPos;
    mGraphicsItem->setPos(newPos.toPxQPointF());
    mGraphicsItem->updateCacheAndRepaint();
    foreach (SI_SymbolPin* pin, mPins)
        pin->updatePosition();
}

void SI_Symbol::setRotation(const Angle& newRotation) throw (Exception)
{
    mRotation = newRotation;
    mGraphicsItem->setRotation(-newRotation.toDeg());
    mGraphicsItem->updateCacheAndRepaint();
    foreach (SI_SymbolPin* pin, mPins)
        pin->updatePosition();
}

/*****************************************************************************************
 *  General Methods
 ****************************************************************************************/

void SI_Symbol::addToSchematic(GraphicsScene& scene) throw (Exception)
{
    mComponentInstance->registerSymbol(*this);
    scene.addItem(*mGraphicsItem);
    foreach (SI_SymbolPin* pin, mPins)
        pin->addToSchematic(scene);
}

void SI_Symbol::removeFromSchematic(GraphicsScene& scene) throw (Exception)
{
    mComponentInstance->unregisterSymbol(*this);
    scene.removeItem(*mGraphicsItem);
    foreach (SI_SymbolPin* pin, mPins)
        pin->removeFromSchematic(scene);
}

XmlDomElement* SI_Symbol::serializeToXmlDomElement() const throw (Exception)
{
    if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);

    QScopedPointer<XmlDomElement> root(new XmlDomElement("symbol"));
    root->setAttribute("uuid", mUuid);
    root->setAttribute("component_instance", mComponentInstance->getUuid());
    root->setAttribute("symbol_item", mSymbVarItem->getUuid());
    XmlDomElement* position = root->appendChild("position");
    position->setAttribute("x", mPosition.getX());
    position->setAttribute("y", mPosition.getY());
    position->setAttribute("rotation", mRotation);
    return root.take();
}

/*****************************************************************************************
 *  Helper Methods
 ****************************************************************************************/

Point SI_Symbol::mapToScene(const Point& relativePos) const noexcept
{
    return (mPosition + relativePos).rotated(mRotation, mPosition);
}

bool SI_Symbol::getAttributeValue(const QString& attrNS, const QString& attrKey,
                                       bool passToParents, QString& value) const noexcept
{
    if ((attrNS == QLatin1String("SYM")) || (attrNS.isEmpty()))
    {
        if (attrKey == QLatin1String("NAME"))
            return value = getName(), true;
    }

    if ((attrNS != QLatin1String("SYM")) && (passToParents))
    {
        if (mComponentInstance->getAttributeValue(attrNS, attrKey, false, value))
            return true;
        if (mSchematic.getAttributeValue(attrNS, attrKey, true, value))
            return true;
    }

    return false;
}

/*****************************************************************************************
 *  Inherited from SI_Base
 ****************************************************************************************/

QPainterPath SI_Symbol::getGrabAreaScenePx() const noexcept
{
    return mGraphicsItem->sceneTransform().map(mGraphicsItem->shape());
}

void SI_Symbol::setSelected(bool selected) noexcept
{
    SI_Base::setSelected(selected);
    mGraphicsItem->update();
    foreach (SI_SymbolPin* pin, mPins)
        pin->setSelected(selected);
}

/*****************************************************************************************
 *  Private Slots
 ****************************************************************************************/

void SI_Symbol::schematicOrComponentAttributesChanged()
{
    mGraphicsItem->updateCacheAndRepaint();
}

/*****************************************************************************************
 *  Private Methods
 ****************************************************************************************/

bool SI_Symbol::checkAttributesValidity() const noexcept
{
    if (mSymbVarItem == nullptr)        return false;
    if (mSymbol == nullptr)             return false;
    if (mUuid.isNull())                 return false;
    if (mComponentInstance == nullptr)    return false;
    return true;
}

/*****************************************************************************************
 *  End of File
 ****************************************************************************************/

} // namespace project
