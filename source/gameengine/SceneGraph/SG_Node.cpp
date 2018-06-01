/* ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/SceneGraph/SG_Node.cpp
 *  \ingroup bgesg
 */


#include "SG_Node.h"
#include "SG_Familly.h"
#include "SG_Controller.h"

#include "CM_List.h"

#include "BLI_utildefines.h"

static CM_ThreadMutex transformMutex;

SG_Node::SG_Node(void *clientobj, void *clientinfo, SG_Callbacks& callbacks)
	:SG_QList(),
	m_clientObject(clientobj),
	m_clientInfo(clientinfo),
	m_callbacks(callbacks),
	m_parent(nullptr),
	m_localPosition(mt::zero3),
	m_localRotation(mt::mat3::Identity()),
	m_localScaling(mt::one3),
	m_worldPosition(mt::zero3),
	m_worldRotation(mt::mat3::Identity()),
	m_worldScaling(mt::one3),
	m_parent_relation(nullptr),
	m_familly(new SG_Familly()),
	m_modified(true),
	m_dirty(DIRTY_NONE)
{
}

SG_Node::SG_Node(const SG_Node & other)
	:SG_QList(),
	m_clientObject(other.m_clientObject),
	m_clientInfo(other.m_clientInfo),
	m_callbacks(other.m_callbacks),
	m_children(other.m_children),
	m_parent(other.m_parent),
	m_localPosition(other.m_localPosition),
	m_localRotation(other.m_localRotation),
	m_localScaling(other.m_localScaling),
	m_worldPosition(other.m_worldPosition),
	m_worldRotation(other.m_worldRotation),
	m_worldScaling(other.m_worldScaling),
	m_parent_relation(other.m_parent_relation->NewCopy()),
	m_familly(new SG_Familly()),
	m_dirty(DIRTY_NONE)
{
}

SG_Node::~SG_Node()
{
}

SG_Node *SG_Node::GetReplica()
{
	SG_Node *replica = new SG_Node(*this);
	if (replica == nullptr) {
		return nullptr;
	}

	ProcessSGReplica(&replica);

	return replica;
}

void SG_Node::ProcessSGReplica(SG_Node **replica)
{
	// Apply the replication call back function.
	if (!ActivateReplicationCallback(*replica)) {
		delete (*replica);
		*replica = nullptr;
		return;
	}

	// clear the replica node of it's parent.
	(*replica)->m_parent = nullptr;

	if (!m_children.empty()) {
		// if this node has children, the replica has too, so clear and clone children
		(*replica)->ClearSGChildren();

		for (SG_Node *childnode : m_children) {
			SG_Node *replicanode = childnode->GetReplica();
			if (replicanode) {
				(*replica)->AddChild(replicanode);
			}
		}
	}
	// Nodes without children and without client object are
	// not worth to keep, they will just take up CPU
	// This can happen in partial replication of hierarchy
	// during group duplication.
	if ((*replica)->m_children.empty() &&
	    (*replica)->GetClientObject() == nullptr) {
		delete (*replica);
		*replica = nullptr;
	}
}

void SG_Node::Destruct()
{
	// Not entirely sure what Destruct() expects to happen.
	// I think it probably means just to call the DestructionCallback
	// in the right order on all the children - rather than free any memory

	// We'll delete m_parent_relation now anyway.

	m_parent_relation.reset(nullptr);

	for (SG_Node *childnode : m_children) {
		// call the SG_Node destruct method on each of our children }-)
		childnode->Destruct();
	}

	ActivateDestructionCallback();
}

const SG_Node *SG_Node::GetRootSGParent() const
{
	return (m_parent ? m_parent->GetRootSGParent() : this);
}

bool SG_Node::IsAncessor(const SG_Node *child) const
{
	return (!child->m_parent) ? false :
	       (child->m_parent == this) ? true : IsAncessor(child->m_parent);
}

const NodeList& SG_Node::GetChildren() const
{
	return m_children;
}

void SG_Node::ClearSGChildren()
{
	m_children.clear();
}

SG_Node *SG_Node::GetParent() const
{
	return m_parent;
}

void SG_Node::SetParent(SG_Node *parent)
{
	m_parent = parent;
	if (parent) {
		SetFamilly(parent->GetFamilly());
	}
}

void SG_Node::DisconnectFromParent()
{
	if (m_parent) {
		m_parent->RemoveChild(this);
		m_parent = nullptr;
		SetFamilly(std::make_shared<SG_Familly>());
	}
}

bool SG_Node::IsVertexParent()
{
	if (m_parent_relation) {
		return m_parent_relation->IsVertexRelation();
	}
	return false;
}

bool SG_Node::IsSlowParent()
{
	if (m_parent_relation) {
		return m_parent_relation->IsSlowRelation();
	}
	return false;
}

void SG_Node::AddChild(SG_Node *child)
{
	m_children.push_back(child);
	child->SetParent(this);
}

void SG_Node::RemoveChild(SG_Node *child)
{
	CM_ListRemoveIfFound(m_children, child);
}

SG_Callbacks& SG_Node::GetCallBackFunctions()
{
	return m_callbacks;
}

void *SG_Node::GetClientObject() const
{
	return m_clientObject;
}

void SG_Node::SetClientObject(void *clientObject)
{
	m_clientObject = clientObject;
}

void *SG_Node::GetClientInfo() const
{
	return m_clientInfo;
}
void SG_Node::SetClientInfo(void *clientInfo)
{
	m_clientInfo = clientInfo;
}

void SG_Node::SetModified()
{
	if (m_modified) {
		return;
	}

	m_modified = true;
	for (SG_Node *child : m_children) {
		child->SetModified();
	}
}

void SG_Node::ClearDirty(DirtyFlag flag)
{
	m_dirty &= ~flag;
}

void SG_Node::SetParentRelation(SG_ParentRelation *relation)
{
	m_parent_relation.reset(relation);
	SetModified();
}

SG_ParentRelation *SG_Node::GetParentRelation()
{
	return m_parent_relation.get();
}

/**
 * Update Spatial Data.
 * Calculates WorldTransform., (either doing its self or using the linked SGControllers)
 */
void SG_Node::UpdateSpatial()
{
	if (!m_modified) {
		return;
	}

	if (m_parent) {
		m_parent->UpdateSpatial();
		// Ask the parent_relation object owned by this class to update our world coordinates.
		m_parent_relation->UpdateChildCoordinates(this, m_parent);
	}
	else {
		SetWorldFromLocalTransform();
	}
	ActivateUpdateTransformCallback();

	m_modified = false;
	m_dirty = DIRTY_ALL;
}

void SG_Node::SetWorldFromLocalTransform()
{
	m_worldPosition = m_localPosition;
	m_worldScaling = m_localScaling;
	m_worldRotation = m_localRotation;
}

bool SG_Node::IsNegativeScaling() const
{
	UpdateSpatial();
	return (m_worldScaling.x * m_worldScaling.y * m_worldScaling.z) < 0.0f;
}

const std::shared_ptr<SG_Familly>& SG_Node::GetFamilly() const
{
	BLI_assert(m_familly != nullptr);

	return m_familly;
}

void SG_Node::SetFamilly(const std::shared_ptr<SG_Familly>& familly)
{
	BLI_assert(familly != nullptr);

	m_familly = familly;
	for (SG_Node *child : m_children) {
		child->SetFamilly(m_familly);
	}
}

bool SG_Node::IsModified()
{
	return m_modified;
}

bool SG_Node::IsDirty(DirtyFlag flag)
{
	return (m_dirty & flag);
}

bool SG_Node::ActivateReplicationCallback(SG_Node *replica)
{
	if (m_callbacks.m_replicafunc) {
		// Call client provided replication func
		if (m_callbacks.m_replicafunc(replica, m_clientObject, m_clientInfo) == nullptr) {
			return false;
		}
	}
	return true;
}

void SG_Node::ActivateDestructionCallback()
{
	if (m_callbacks.m_destructionfunc) {
		// Call client provided destruction function on this!
		m_callbacks.m_destructionfunc(this, m_clientObject, m_clientInfo);
	}
	else {
		// no callback but must still destroy the node to avoid memory leak
		delete this;
	}
}

void SG_Node::ActivateUpdateTransformCallback()
{
	if (m_callbacks.m_updatefunc) {
		// Call client provided update func.
// 		transformMutex.Lock(); // TODO
		m_callbacks.m_updatefunc(this, m_clientObject, m_clientInfo);
// 		transformMutex.Unlock();
	}
}
