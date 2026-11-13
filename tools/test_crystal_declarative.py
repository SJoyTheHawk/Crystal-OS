#!/usr/bin/env python3
"""Validate and package the Phase 16 declarative dashboard example."""
import json
from pathlib import Path
import tempfile
import crystal

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'examples/declarative_dashboard'
SUPPORTED = {'container', 'row', 'col', 'scroll_view', 'label', 'button',
             'button_matrix', 'switch', 'slider', 'arc', 'tabview', 'image',
             'list', 'chart', 'textarea', 'spinner', 'dialog'}
EXPRESSIONS = {'time.now', 'crystal.service.time.now',
               'network.status.state', 'crystal.service.net.status.state',
               'timer.remaining', 'crystal.service.timer.remaining',
               'timer.running', 'crystal.service.timer.running',
               'timer.paused', 'crystal.service.timer.paused'}
ACTIONS = {'timer.pause', 'timer.resume', 'timer.reset',
           'crystal.service.timer.pause()', 'crystal.service.timer.resume()',
           'crystal.service.timer.reset()'}


def visit(node, ids, types, actions):
    if node.get('type') == 'tab':
        if not node.get('title'):
            raise AssertionError('tab needs a title')
        if node.get('id'):
            ids.add(node['id'])
        for child in node.get('children', []):
            visit(child, ids, types, actions)
        return
    kind = node.get('type')
    assert kind in SUPPORTED, f'unsupported example widget: {kind}'
    types.add(kind)
    action = node.get('on_click') or node.get('on_change')
    if action:
        actions.append(action)
    if node.get('id'):
        assert node['id'] not in ids, f'duplicate id: {node["id"]}'
        ids.add(node['id'])
    for child in node.get('children', []):
        visit(child, ids, types, actions)


def main():
    manifest = crystal.load_manifest(PACKAGE / 'app.yaml')
    ui = json.loads((PACKAGE / manifest['entry']).read_text(encoding='utf-8'))
    bindings = json.loads((PACKAGE / ui['bindings']).read_text(encoding='utf-8'))['bindings']
    ids, types, actions = set(), set(), []
    visit(ui['screen'], ids, types, actions)
    assert {'tabview', 'button_matrix', 'label', 'button', 'switch', 'slider', 'image',
            'list', 'chart', 'textarea', 'spinner', 'dialog'} <= types
    assert (PACKAGE / 'assets/weather.cr16').is_file()
    for example in ('clock.json', 'weather.json', 'calculator.json'):
        visual = json.loads((PACKAGE / 'visual' / example).read_text(encoding='utf-8'))
        visit(visual['screen'], set(), set(), [])
    for binding in bindings:
        assert binding['id'] in ids
        assert binding['property'] in {'text', 'visible'}
        assert '{{' in binding['value'] and '}}' in binding['value']
        expression = binding['value'].split('{{', 1)[1].split('}}', 1)[0].strip()
        path = expression.split('==', 1)[0].split('!=', 1)[0].split('| format', 1)[0].strip()
        assert path in EXPRESSIONS, f'unsupported sample binding: {path}'
    for action in actions:
        if isinstance(action, dict):
            assert action['call'] in {'ui.set_focus', 'ui.list_add', 'ui.chart_set_data', 'ui.set_hidden'}
            assert action['target'] in ids
            continue
        if action.startswith('timer.arm(') or action.startswith('crystal.service.timer.arm('):
            argument = action[action.find('(') + 1:-1]
            assert argument.isdigit() and 0 < int(argument) <= 86400
        else:
            assert action in ACTIONS, f'unsupported sample action: {action}'
    assert manifest['permissions'].get('system_timer') is True
    with tempfile.TemporaryDirectory(prefix='crystal-declarative-') as directory:
        artifact = Path(directory) / 'dashboard.capp'
        crystal.pack(PACKAGE, artifact)
        inspected = crystal.inspect(artifact)
        assert inspected['manifest']['runtime'] == 'declarative'
        assert {'ui.json', 'bindings.json'} <= set(inspected['files'])


if __name__ == '__main__':
    main()
